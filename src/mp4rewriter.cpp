#include "mp4rewriter.h"

#include <algorithm>

#include <fcntl.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <sys/endian.h>
#include <linux/stat.h>
#endif

#include "mp4trimmer.h"

#ifdef __ANDROID__
#define _I(...) do {} while(0)
#define _W(...) do {} while(0)
#define _E(...) do {} while(0)
#endif

#ifdef __APPLE__
#define _I printf
#define _W printf
#define _E printf
#endif

// #define NO_AUDIO 1

using namespace std;

MP4Rewriter::MP4Rewriter()
    : mPath()
    , mFD(-1)
    , mOffset(0)
    , mIsWritingVideoTrack(false)
    , mIsWritingAudioTrack(false)
    , mMP4Info(NULL)
    //, mTrackInfo(NULL)
{}

MP4Rewriter::~MP4Rewriter()
{}

int
MP4Rewriter::setOutputPath(const string path)
{
	mPath = path;

	return 0;
}

int
MP4Rewriter::open()
{
	if (mFD == -1) {
		mFD = ::open(mPath.c_str(), O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
	}

	return 0;
}

int
MP4Rewriter::close()
{
	::close(mFD);
	return 0;
}

int
MP4Rewriter::copyData(int src, off_t posi, int size, const string srcName)
{
	_W("src(%d): %lld + %d \n", src, posi, size);
    lseek(src, posi, SEEK_SET);
    int left = size;
    char buff[4096];
    while (left) {
        ssize_t r = read(src, buff, std::min((int)sizeof(buff), left));
        if (r > 0) {
            left -= r;
            while (r > 0) {
                ssize_t w = ::write(mFD, buff, r);
                if (w > r) {
                    _E("write more then wanted, this is impossible, %ld/%ld", w, r);
                    return -1;
                } else if (w > 0) {
                    r -= w;
                } else {
                    _E("got %ld when write to %s", w, mPath.c_str());
                    return -1;
                }
            }

        } else if (r == 0) {
            _W("read 0 byte from %s, this is strange, %d/%d", srcName.c_str(), left, size);
            left = 0;
            return -1;

        } else {
            _E("got %ld when read from %s", r, srcName.c_str());
            return -1;
        }
    }

    mOffset += size;

    return 0;
}


int MP4Rewriter::write(MP4Info *mp4info)
{
    mMP4Info = mp4info;

	// write ftyp
	writeFtypBox();

	// write mdat
	int32_t mdatLen = 8; // ....mdat
	int32_t mediaDataSize = mp4info->trimCeaseOffset - mp4info->trimBeginOffset;
	mdatLen += mediaDataSize;
	writeInt32(mdatLen);
	writeFourcc("mdat");
	int srcFD = ::open(mp4info->mFilePath.c_str(), O_RDONLY);
	copyData(srcFD, mp4info->trimBeginOffset, mediaDataSize, mp4info->mFilePath);
    ::close(srcFD);

	// write moov
	writeMoovBox();

	return 0;
}

size_t MP4Rewriter::write(const void* data, size_t size, size_t nmemb)
{
	size_t bytes = size * nmemb;
	::write(mFD, data, bytes);
	mOffset += bytes;
	return bytes;
}

void MP4Rewriter::write(const void *data, size_t size) {
    write(data, 1, size);
}

void MP4Rewriter::writeInt8(int8_t x) {
    write(&x, 1, 1);
}

void MP4Rewriter::writeInt16(int16_t x) {
    x = htons(x);
    write(&x, 1, 2);
}

void MP4Rewriter::writeInt32(int32_t x)
{
	x = htonl(x);
	write(&x, 1, 4);
}

static uint64_t hton64(uint64_t x)
{
    return ((uint64_t)htonl(x & 0xffffffff) << 32) | htonl(x >> 32);
}

void MP4Rewriter::writeInt64(int64_t x) {
    x = hton64(x);
    write(&x, 1, 8);
}

void MP4Rewriter::writeFourcc(const char *fourcc) {
    write(fourcc, 1, 4);
}

void MP4Rewriter::writeCString(const char* str) {
	write(str, 1, strlen(str) + 1);
}

void MP4Rewriter::beginBox(const char *fourcc)
{
	BoxInfo bi = {mOffset, fourcc};
	mBoxes.push_back(bi);

	writeInt32(0);
	writeFourcc(fourcc);
}

void MP4Rewriter::endBox()
{
	BoxInfo bi = *--mBoxes.end();
	mBoxes.erase(--mBoxes.end());

    lseek(mFD, bi.offset, SEEK_SET);
    int32_t boxSize = mOffset - bi.offset;
    writeInt32(boxSize);
    mOffset -= 4;
    lseek(mFD, mOffset, SEEK_SET);
    _I("box %s end with size %d - %08x; will seek to %lld \n", bi.name, boxSize, boxSize, mOffset);
}

void MP4Rewriter::writeFtypBox()
{
    beginBox("ftyp");
    writeFourcc("mp42");
    writeInt32(0);
    writeFourcc("isom");
    writeFourcc("mp42");
    endBox();
}

void MP4Rewriter::writeMoovBox()
{
    beginBox("moov");

    writeMvhdBox();

    // write video track
    // mTrackInfo = mMP4Info->mVideoTrackInfo;
    beginVideoTrack();
    writeTrackInfo();

    // write audio track
    // mTrackInfo = mMP4Info->mAudioTrackInfo;
#ifndef NO_AUDIO
    beginAudioTrack();
    writeTrackInfo();
#endif
    endBox();
}

static uint32_t GetMpeg4Time() {
    time_t now = time(NULL);
    // MP4 file uses time counting seconds since midnight, Jan. 1, 1904
    // while time function returns Unix epoch values which starts
    // at 1970-01-01. Lets add the number of seconds between them
    uint32_t mpeg4Time = now + (66 * 365 + 17) * (24 * 60 * 60);
    return mpeg4Time;
}

void MP4Rewriter::writeMvhdBox()
{
    uint32_t now = GetMpeg4Time();
    beginBox("mvhd");
    writeInt32(0);             // version=0, flags=0
    writeInt32(now);           // creation time
    writeInt32(now);           // modification time
    writeInt32(getTimeScale());    // mvhd timescale

    writeInt32(getDuration());

    writeInt32(0x10000);       // rate: 1.0
    writeInt16(0x100);         // volume
    writeInt16(0);             // reserved
    writeInt32(0);             // reserved
    writeInt32(0);             // reserved
    writeCompositionMatrix(0); // TODO matrix
    writeInt32(0);             // predefined
    writeInt32(0);             // predefined
    writeInt32(0);             // predefined
    writeInt32(0);             // predefined
    writeInt32(0);             // predefined
    writeInt32(0);             // predefined
#ifndef NO_AUDIO
    writeInt32(getTrackCount() + 1);  // nextTrackID
#else
    writeInt32(1);
#endif
    endBox();  // mvhd
}

/*
 * MP4 file standard defines a composition matrix:
 * | a  b  u |
 * | c  d  v |
 * | x  y  w |
 *
 * the element in the matrix is stored in the following
 * order: {a, b, u, c, d, v, x, y, w},
 * where a, b, c, d, x, and y is in 16.16 format, while
 * u, v and w is in 2.30 format.
 */
void MP4Rewriter::writeCompositionMatrix(int degrees) {
    uint32_t a = 0x00010000;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0x00010000;
    switch (degrees) {
        case 0:
            break;
        case 90:
            a = 0;
            b = 0x00010000;
            c = 0xFFFF0000;
            d = 0;
            break;
        case 180:
            a = 0xFFFF0000;
            d = 0xFFFF0000;
            break;
        case 270:
            a = 0;
            b = 0xFFFF0000;
            c = 0x00010000;
            d = 0;
            break;
        default:
            _W("Should never reach this unknown rotation");
            break;
    }

    writeInt32(a);           // a
    writeInt32(b);           // b
    writeInt32(0);           // u
    writeInt32(c);           // c
    writeInt32(d);           // d
    writeInt32(0);           // v
    writeInt32(0);           // x
    writeInt32(0);           // y
    writeInt32(0x40000000);  // w
}

void MP4Rewriter::writeTrackInfo()
{
    uint32_t now = GetMpeg4Time();

    beginBox("trak");
    {
        writeTkhdBox(now);
        beginBox("mdia");
        {
            writeMdhdBox(now);
            writeHdlrBox();
            beginBox("minf");
            {
                if (mIsWritingVideoTrack) {
                	writeVmhdBox();
                }
                else {
                	writeSmhdBox();
                }
                writeDinfBox();
                writeStblBox();
            }
            endBox();
        }
        endBox();
    }
    endBox();
}

void MP4Rewriter::writeTkhdBox(uint32_t now)
{
    beginBox("tkhd");
    
    writeInt32(0x07);          // version=0, flags=7
    writeInt32(now);           // creation time
    writeInt32(now);           // modification time
    writeInt32(getTrackID());
    writeInt32(0);             // reserved

    writeInt32(getDuration());             // duration, in mvhd timescale

    writeInt32(0);             // reserved
    writeInt32(0);             // reserved
    writeInt16(0);             // layer
    writeInt16(0);             // alternate group
    writeInt16(mIsWritingVideoTrack ? 0 : 0x100); // volume
    writeInt16(0);             // reserved

    writeCompositionMatrix(0);  // TODO 

    if (mIsWritingVideoTrack) {
    	int32_t width = getVideoWidth();
    	int32_t height = getVideoHeight();
    	writeInt32(width);
    	writeInt32(height);
    }
    else {
    	writeInt32(0);
    	writeInt32(0);
    }

    endBox();
}

void MP4Rewriter::writeMdhdBox(uint32_t now)
{
    beginBox("mdhd");

    writeInt32(0);              // version=0, flags=0
    writeInt32(now);            // creation time
    writeInt32(now);            // modification time

    writeInt32(getTrackTimeScale()); // timescale

    uint32_t duration = (getDurationUs() * getTrackTimeScale() + 5E5) / 1E6;
    writeInt32(duration);
    writeInt16(0);              // language code
    writeInt16(0);              // predefined

    endBox();
}

void MP4Rewriter::writeHdlrBox()
{
    beginBox("hdlr");

    writeInt32(0);              // version=0, flags=0
    writeInt32(0);              // component type
    writeFourcc(mIsWritingVideoTrack ? "vide" : "soun"); // component subtype
    writeInt32(0);              // reserved
    writeInt32(0);              // reserved
    writeInt32(0);              // reserved
    writeCString(mIsWritingVideoTrack ? "VideoHandle" : "SoundHandle");  // name

    endBox();
}

void MP4Rewriter::writeVmhdBox()
{
    beginBox("vmhd");

    writeInt32(1);          // version=0, flags=1
    writeInt16(0);          // graphics mode
    writeInt16(0);          // opcolor
    writeInt16(0);
    writeInt16(0);

    endBox();
}

void MP4Rewriter::writeSmhdBox()
{
    beginBox("smhd");

    writeInt32(0);          // version=0, flags=0
    writeInt16(0);          // balance
    writeInt16(0);          // reserved

    endBox();
}

void MP4Rewriter::writeDinfBox()
{
    beginBox("dinf");
    {
        beginBox("dref");
        {
            writeInt32(0); // version=0, flags=0
            writeInt32(1); // entry count: url or urn
            beginBox("url ");
            {
                writeInt32(1); // version=0, flags=1 (self contained)
            }
            endBox();
        }
        endBox();
    }
    //
    endBox();
}


int MP4Rewriter::writeStblBox()
{
    beginBox("stbl");
    {
        beginBox("stsd");
        {
            writeInt32(0);      // version=0, flags=0
            writeInt32(1);      // entryCount
            if (mIsWritingVideoTrack) {
                writeVideoFourCCBox();
            }
            else {
                writeAudioFourCCBox();
            }
        }
        endBox();

        // int32_t sampleCount = getSampleCount();

        // stts
        beginBox("stts");   // sample duration
        {
            writeInt32(0);      // version=0, flags=0

            decltype(mMP4Info->mVideoTrackInfo->stts)* stts = nullptr;
            decltype(mMP4Info->trimBeginVideoID) id;

            if (mIsWritingVideoTrack) {
                stts = &mMP4Info->mVideoTrackInfo->stts;
                id = mMP4Info->trimBeginVideoID;
            }
            else {
                stts = &mMP4Info->mAudioTrackInfo->stts;
                id = mMP4Info->trimBeginAudioID;
            }

            auto it = stts->begin();
            while (id > it->count && it != stts->end()) {
                id -= it->count;
                ++it;
            }
            if (it == stts->end()) {
                _E("UGLY BAD %s stts data! \n", (mIsWritingVideoTrack ? "video" : "audio"));
                return -9527;
            }

            auto yaid = getSampleCount() + id;
            auto yait = it;

            while (yaid > yait->count && yait != stts->end()) {
                yaid -= yait->count;
                ++yait;
            }
            if (yait == stts->end()) {
                _E("UGLY BAD %s stts data! \n", (mIsWritingVideoTrack ? "video" : "audio"));
                return -19527;
            }

            if (it < yait) {
                writeInt32(yait - it + ((it->count - id > 1) ? 2 : 1)); // ?
                writeInt32(1);
                writeInt32(it->delta);

                if (it->count - id > 1) {
                    writeInt32(it->count - id - 1);
                    writeInt32(it->delta);
                }
                for (++it; it < yait; ++it) {
                    writeInt32(it->count);
                    writeInt32(it->delta);
                }
                writeInt32(yaid);
                writeInt32(yait->delta);
            }
            else {
                // TODO if yaid - id - 1 == 0
                writeInt32(2);
                writeInt32(1);
                writeInt32(it->delta);
                writeInt32(yaid - id - 1);
                writeInt32(it->delta);
            }

#if 0
            writeInt32(2);      //
            int32_t sampleDelta = getSampleDelta();
            writeInt32(1);
            writeInt32(sampleDelta);
            writeInt32(sampleCount - 1);
            writeInt32(sampleDelta);
#endif
        }
        endBox();
#if 0
        if (mTrackInfo->mIsVideo) {
            writeInt32(1);
            writeInt32(5000);   // TODO
            writeInt32(videoSampleCount - 1);
            writeInt32(5000);   // TODO
        }
        else {
            writeInt32(1);
            writeInt32(1024);   // TODO
            writeInt32(audioSampleCount - 1);
            writeInt32(1024);   // TODO
        }
        endBox();
#endif

        // ctts
        if (mIsWritingVideoTrack && mMP4Info->mVideoTrackInfo->ctts.size()) {
            auto& ctts = mMP4Info->mVideoTrackInfo->ctts;

            beginBox("ctts");
            {
                auto accu = 0;
                auto it = ctts.begin();

                for (; accu + it->count < mMP4Info->trimBeginVideoID; ++it, accu += it->count) {}
                auto bit = it;
                auto baccu = accu;

                for (; accu + it->count < mMP4Info->trimCeaseVideoID; ++it, accu += it->count) {}
                auto cit = it;
                auto caccu = accu;

                int32_t count = 0;
                if (bit != cit) {
                    count += cit - bit;
                    if (baccu + bit->count - mMP4Info->trimBeginVideoID > 0) {
                        count += 2;
                    }
                    else {
                        count += 1;
                    }
                }
                else {
                    count = 2;
                }


                writeInt32(0); // version=0, flags=0

                writeInt32(count);

                writeInt32(1);
                writeInt32(10000);

                if (baccu + bit->count - mMP4Info->trimBeginVideoID > 0) {
                    writeInt32(baccu + bit->count - mMP4Info->trimBeginVideoID);
                    writeInt32(bit->delta);
                }

                for (++bit; bit < cit; ++bit) {
                    writeInt32(bit->count);
                    writeInt32(bit->delta);
                }

                writeInt32(mMP4Info->trimCeaseVideoID - caccu);
                writeInt32(cit->delta);

            }
            endBox();
        }

        // stss
#if 1
        writeStssBox();
#else
        if (mTrackInfo->mIsVideo) {
            beginBox("stss");
            writeInt32(0);      // version=0, falgs=0

            vector<int32_t>::iterator bi = lower_bound(mTrackInfo->stss.begin(), mTrackInfo->stss.end(), mMP4Info->trimBeginVideoID);
            vector<int32_t>::iterator ci = lower_bound(mTrackInfo->stss.begin(), mTrackInfo->stss.end(), mMP4Info->trimCeaseVideoID);

            _I("for stss begin iterator: %d \n", *bi);
            _I("for stss cease iterator: %d \n", *ci);

            writeInt32(ci - bi);
            int32_t distance = *bi - mTrackInfo->stss[0];
            for (; bi < ci; ++bi) {
                writeInt32(*bi - distance);
            }

            endBox();
        }
#endif

        // stsz
#if 1
        writeStszBox();
#else
        beginBox("stsz");
        writeInt32(0);          // version=0, flags=0
        writeInt32(0);          // sample-size=0
        if (mTrackInfo->mIsVideo) {
            writeInt32(videoSampleCount);
            for (int i = mMP4Info->trimBeginVideoID; i < mMP4Info->trimCeaseVideoID; ++i) {
                writeInt32(mTrackInfo->stsz[i]);
            }
        }
        else {
            writeInt32(audioSampleCount);
            for (int i = mMP4Info->trimBeginAudioID; i < mMP4Info->trimCeaseAudioID; ++i) {
                writeInt32(mTrackInfo->stsz[i]);
            }

        }
        endBox();
#endif

        // stsc samplt to chunk
        beginBox("stsc");
        {
            writeInt32(0);          // version=0, flags=0

            decltype(mMP4Info->mVideoTrackInfo) trackInfo = nullptr;

            decltype(mMP4Info->trimBeginVideoChunk)* trimBeginChunk = nullptr;
            decltype(mMP4Info->trimCeaseVideoChunk)* trimCeaseChunk = nullptr;

            decltype(mMP4Info->trimBeginVideoID) trimBeginID;
            decltype(mMP4Info->trimCeaseVideoID) trimCeaseID;

            if (mIsWritingVideoTrack) {
                trackInfo = mMP4Info->mVideoTrackInfo;
                trimBeginChunk = &mMP4Info->trimBeginVideoChunk;
                trimCeaseChunk = &mMP4Info->trimCeaseVideoChunk;
                trimBeginID = mMP4Info->trimBeginVideoID;
                trimCeaseID = mMP4Info->trimCeaseVideoID;
            }
            else {
                trackInfo = mMP4Info->mAudioTrackInfo;
                trimBeginChunk = &mMP4Info->trimBeginAudioChunk;
                trimCeaseChunk = &mMP4Info->trimCeaseAudioChunk;
                trimBeginID = mMP4Info->trimBeginAudioID;
                trimCeaseID = mMP4Info->trimCeaseAudioID;
            }

            {
                auto bit = upper_bound(
                            trackInfo->stsc.begin(),
                            trackInfo->stsc.end(),
                            (*trimBeginChunk) - trackInfo->stco.begin() + 1,
                            compareStscChunkIndex);
                --bit;
                auto cit = upper_bound(
                            trackInfo->stsc.begin(),
                            trackInfo->stsc.end(),
                            (*trimCeaseChunk) - trackInfo->stco.begin() + 1,
                            compareStscChunkIndex);
                --cit;

                if (bit == cit) {
                    if ((*trimCeaseChunk) == (*trimBeginChunk)) {
                        writeInt32(1); // count

                        writeInt32(1); // first chunk
                        writeInt32(trimCeaseID - trimBeginID + 1);
                        writeInt32(1);
                    }
                    else {
                        writeInt32(2 + ((trimCeaseID - trimBeginID > 1) ? 1 : 0));

                        //
                        writeInt32(1); // first chunk
                        writeInt32(bit->samplesPerChunk - (trimBeginID - (*trimBeginChunk)->firstSampleIndex) + 1); // TODO
                        writeInt32(1);

                        if (trimCeaseID - trimBeginID > 1) {
                            writeInt32(2); // begin chunk id
                            //writeInt32((*trimCeaseChunk) - (*trimBeginChunk) - 1);
                            writeInt32(bit->samplesPerChunk);
                            writeInt32(1);
                        }

                        // mMP4Info->trimCeaseVideoChunk
                        writeInt32((*trimCeaseChunk) - (*trimBeginChunk) + 1); // last chunk
                        writeInt32(trimCeaseID - 1 - (*trimCeaseChunk)->firstSampleIndex);
                        writeInt32(1);
                    }
                }
                else {
                    int32_t const chunkIndexOffset = (*trimBeginChunk) - trackInfo->stco.begin();

                    int32_t count = cit - bit + 1;
                    if ((*trimBeginChunk) - trackInfo->stco.begin() + 2 < (bit + 1)->firstChunkIndex) {
                        count += 1;
                    }
                    if ((*trimCeaseChunk) - trackInfo->stco.begin() + 1 > cit->firstChunkIndex) {
                        count += 1;
                    }
                    if (trimCeaseID - 1 - (*trimCeaseChunk)->firstSampleIndex <= 0) {
                        count -= 1;
                    }
                    writeInt32(count);

                    writeInt32(1);
                    writeInt32(bit->samplesPerChunk - (trimBeginID - 1 - (*trimBeginChunk)->firstSampleIndex));
                    writeInt32(1);

                    if ((*trimBeginChunk) - trackInfo->stco.begin() + 2 < (bit + 1)->firstChunkIndex) {
                        writeInt32(2);
                        writeInt32(bit->samplesPerChunk);
                        writeInt32(1);
                    }

                    for (auto it = bit + 1; it < cit; ++it) {
                        writeInt32(it->firstChunkIndex - chunkIndexOffset);
                        writeInt32(it->samplesPerChunk);
                        writeInt32(1);
                    }

                    if ((*trimCeaseChunk) - trackInfo->stco.begin() + 1 > cit->firstChunkIndex) {
                        writeInt32(cit->firstChunkIndex - chunkIndexOffset);
                        writeInt32(cit->samplesPerChunk);
                        writeInt32(1);
                    }

                    if (trimCeaseID - 1 - (*trimCeaseChunk)->firstSampleIndex > 0) {
                        writeInt32((*trimCeaseChunk) - (*trimBeginChunk) + 1);
                        writeInt32(trimCeaseID - 1 - (*trimCeaseChunk)->firstSampleIndex);
                        writeInt32(1);
                    }
                }

#if 0
                auto it = stsc.begin();
                if (it == stsc.end()) {
                    _E("mal stsc data!\n");
                    return -29527;
                }
                auto nit = it + 1;
                auto c = 0;
                for (; it != stsc.end(); ++it) {
                    if (c + it->samplesPerChunk * ((it + 1)->))
                    c += it->samplesPerChunk;
                }
#endif
            }

            // writeInt32(sampleCount);
            // for (int i = 1; i <= sampleCount; ++i) {
            //     writeInt32(i);
            //     writeInt32(1);
            //     writeInt32(1);
            // }
        }
        endBox();

        // stco
#if 1
        writeStcoBox();
#else
        beginBox("stco");
        writeInt32(0);          // version=0, flags=0

        mMP4Info->postTrimMediaDataOffset = 32; // so far so good
        mMP4Info->postTrimFirstVideoOffset = mMP4Info->postTrimMediaDataOffset;
        mMP4Info->postTrimFirstAudioOffset = mMP4Info->postTrimFirstVideoOffset + mMP4Info->mVideoTrackInfo->stsz[mMP4Info->trimBeginVideoID];

        if (mTrackInfo->mIsVideo) {
            writeInt32(videoSampleCount);
            int32_t distance = mTrackInfo->stco[mMP4Info->trimBeginVideoID] - mMP4Info->postTrimFirstVideoOffset;
            _I("video distance: %d = %d - %d\n", distance, mTrackInfo->stco[mMP4Info->trimBeginVideoID], mMP4Info->postTrimFirstVideoOffset);
            for (int i = mMP4Info->trimBeginVideoID; i < mMP4Info->trimCeaseVideoID; ++i) {
                writeInt32(mTrackInfo->stco[i] -  distance);
            }

        } else {
            writeInt32(audioSampleCount);
            int32_t distance = mTrackInfo->stco[mMP4Info->trimBeginAudioID] - mMP4Info->postTrimFirstAudioOffset;
            _I("audio distance: %d = %d - %d\n", distance, mTrackInfo->stco[mMP4Info->trimBeginAudioID], mMP4Info->postTrimFirstAudioOffset);
            for (int i = mMP4Info->trimBeginAudioID; i < mMP4Info->trimCeaseAudioID; ++i) {
                writeInt32(mTrackInfo->stco[i] -  distance);
            }

        }
        endBox();
#endif
    }
    endBox();

    return 0;
}

void MP4Rewriter::writeVideoFourCCBox()
{
    beginBox("avc1");

    writeInt32(0);           // reserved
    writeInt16(0);           // reserved
    writeInt16(1);           // data ref index
    writeInt16(0);           // predefined
    writeInt16(0);           // reserved
    writeInt32(0);           // predefined
    writeInt32(0);           // predefined
    writeInt32(0);           // predefined

    writeInt16(getAVCWidth());
    writeInt16(getAVCHeight());
    writeInt32(0x480000);    // horiz resolution
    writeInt32(0x480000);    // vert resolution
    writeInt32(0);           // reserved
    writeInt16(1);           // frame count
    writeInt8(0);            // compressor string length
    write("                               ", 31);
    writeInt16(0x18);        // depth
    writeInt16(-1);          // predefined

    // avcC
    {
    	beginBox("avcC");
        char *codecSpec = NULL;
        int32_t codecSpecLen = 0;
        getAVCCodecSpec(&codecSpec, &codecSpecLen);
    	write(codecSpec, codecSpecLen);
    	endBox();
    }

    // pasp
    {
    	beginBox("pasp");
    	writeInt32(1 << 16);    // hspacing
    	writeInt32(1 << 16);    // vspacing
    	endBox();
    }

    endBox();
}

void MP4Rewriter::writeAudioFourCCBox()
{
    beginBox("mp4a");

    char *codecSpec = NULL;
    int32_t codecSpecLen = 0;
    getAACCodecSpec(&codecSpec, &codecSpecLen);
    write(codecSpec, codecSpecLen);

    endBox();
}

void MP4Rewriter::writeStssBox()
{
    if (mIsWritingVideoTrack) {
        beginBox("stss");
        writeInt32(0);      // version=0, falgs=0

        TrackInfo *trackInfo = mMP4Info->mVideoTrackInfo;

        vector<int32_t>::iterator bi = lower_bound(trackInfo->stss.begin(), trackInfo->stss.end(), mMP4Info->trimBeginVideoID);
        vector<int32_t>::iterator ci = lower_bound(trackInfo->stss.begin(), trackInfo->stss.end(), mMP4Info->trimCeaseVideoID);

        _I("for stss begin iterator: %d \n", *bi);
        _I("for stss cease iterator: %d \n", *ci);

        writeInt32(ci - bi);
        int32_t distance = *bi - trackInfo->stss[0];
        for (; bi < ci; ++bi) {
            writeInt32(*bi - distance);
        }

        endBox();
    }
}

void MP4Rewriter::writeStszBox()
{
    beginBox("stsz");
    writeInt32(0);          // version=0, flags=0
    writeInt32(0);          // sample-size=0
    if (mIsWritingVideoTrack) {
        writeInt32(getSampleCount());
        TrackInfo *trackInfo = mMP4Info->mVideoTrackInfo;
        for (int i = mMP4Info->trimBeginVideoID; i < mMP4Info->trimCeaseVideoID; ++i) {
            writeInt32(trackInfo->stsz[i - 1]);
        }
    }
    else {
        writeInt32(getSampleCount());
        TrackInfo *trackInfo = mMP4Info->mAudioTrackInfo;
        for (int i = mMP4Info->trimBeginAudioID; i < mMP4Info->trimCeaseAudioID; ++i) {
            writeInt32(trackInfo->stsz[i - 1]);
        }
    }
    endBox();
}

void MP4Rewriter::writeStcoBox()
{
    beginBox("stco");
    writeInt32(0);          // version=0, flags=0

    mMP4Info->postTrimMediaDataOffset = 32; // so far so good
    mMP4Info->postTrimFirstVideoOffset = mMP4Info->postTrimMediaDataOffset;
    // mMP4Info->postTrimFirstAudioOffset = mMP4Info->postTrimFirstVideoOffset + mMP4Info->mVideoTrackInfo->stsz[mMP4Info->trimBeginVideoID - 1];
    // mMP4Info->trimBeginVideoChunk->firstSampleIndex;

    if (mIsWritingVideoTrack) {
        writeInt32(mMP4Info->trimCeaseVideoChunk - mMP4Info->trimBeginVideoChunk);

        auto it = mMP4Info->trimBeginVideoChunk;
        writeInt32(mMP4Info->postTrimFirstVideoOffset);

        if (it + 1 != mMP4Info->trimCeaseVideoChunk) {
            auto distance = it->chunkOffset - mMP4Info->postTrimFirstVideoOffset;
            auto fix = 0;
            for (auto i = mMP4Info->trimBeginVideoChunk->firstSampleIndex; i < mMP4Info->trimBeginVideoID - 1; ++i) {
                fix += mMP4Info->mVideoTrackInfo->stsz[i];
            }
            distance += fix;
            for (++it; it != mMP4Info->trimCeaseVideoChunk; ++it) {
                writeInt32(it->chunkOffset - distance);
            }
        }

    } else {
        mMP4Info->postTrimFirstAudioOffset = mMP4Info->postTrimFirstVideoOffset;
        mMP4Info->postTrimFirstAudioOffset += mMP4Info->trimBeginAudioChunk->chunkOffset - mMP4Info->trimBeginVideoChunk->chunkOffset;
        for (auto i = mMP4Info->trimBeginVideoChunk->firstSampleIndex; i < mMP4Info->trimBeginVideoID - 1; ++i) {
            mMP4Info->postTrimFirstAudioOffset -= mMP4Info->mVideoTrackInfo->stsz[i];
        }

        writeInt32(mMP4Info->trimCeaseAudioChunk - mMP4Info->trimBeginAudioChunk);
        //TrackInfo *trackInfo = mMP4Info->mAudioTrackInfo;

        auto distance = mMP4Info->trimBeginAudioChunk->chunkOffset - mMP4Info->postTrimFirstAudioOffset;;

        auto it = mMP4Info->trimBeginAudioChunk;
        writeInt32(it->chunkOffset - distance);
        for (++it; it < mMP4Info->trimCeaseAudioChunk; ++it) {
            writeInt32(it->chunkOffset - distance);
        }

    }
    endBox();    
}


MP4CatRewriter::MP4CatRewriter()
    : mCatTask(NULL)
    , mDuration(-1)
    , mVideoSampleCount(-1)
    , mAudioSampleCount(-1)
{}

MP4CatRewriter::~MP4CatRewriter()
{}

int
MP4CatRewriter::write(CatTask * catTask)
{
    mCatTask = catTask;

    writeFtypBox();

    // write mdat
    beginBox("mdat");
    for (list<MP4Info*>::iterator it = catTask->mInfoList.begin(); it != catTask->mInfoList.end(); ++it) {
        MP4Info *mp4info = *it;
        int srcFD = ::open(mp4info->mFilePath.c_str(), O_RDONLY);
        copyData(srcFD, mp4info->mdatOffset + 8, mp4info->mdatSize - 8, mp4info->mFilePath);
        ::close(srcFD);
    }
    endBox();

    // write moov
    writeMoovBox();

    return 0;
}

void
MP4CatRewriter::writeStssBox()
{
    if (isWritingVideoTrack()) {
        beginBox("stss");
        writeInt32(0);      // version=0, falgs=0

        int32_t stssEntryCount = 0;
        for (list<MP4Info*>::iterator it = mCatTask->mInfoList.begin(); it != mCatTask->mInfoList.end(); ++it) {
            stssEntryCount += (*it)->mVideoTrackInfo->stss.size();
        }
        writeInt32(stssEntryCount);

        int32_t off = 0;
        for (list<MP4Info*>::iterator it = mCatTask->mInfoList.begin(); it != mCatTask->mInfoList.end(); ++it) {
            MP4Info *mp4info = *it;
            TrackInfo *trackInfo = mp4info->mVideoTrackInfo;
            for (vector<int32_t>::iterator i = trackInfo->stss.begin(); i != trackInfo->stss.end(); ++i) {
                writeInt32(*i + off);
            }
            off += trackInfo->stsz.size();
        }

        endBox();
    }
}

void
MP4CatRewriter::writeStszBox()
{
    beginBox("stsz");
    writeInt32(0);          // version=0, flags=0
    writeInt32(0);          // sample-size=0

    writeInt32(getSampleCount());

    for (list<MP4Info*>::iterator it = mCatTask->mInfoList.begin(); it != mCatTask->mInfoList.end(); ++it) {
        MP4Info *mp4info = *it;
        TrackInfo *trackInfo = NULL;

        if (isWritingVideoTrack()) {
            trackInfo = mp4info->mVideoTrackInfo;
        }
        else {
            trackInfo = mp4info->mAudioTrackInfo;
        }

        for (vector<int>::iterator i = trackInfo->stsz.begin(); i != trackInfo->stsz.end(); ++i) {
            writeInt32(*i);
        }
    }

    endBox();
}

void
MP4CatRewriter::writeStcoBox()
{
    beginBox("stco");
    writeInt32(0);          // version=0, flags=0

    writeInt32(getSampleCount());

    int32_t off = 32; // so far so good // TODO
    for (list<MP4Info*>::iterator it = mCatTask->mInfoList.begin(); it != mCatTask->mInfoList.end(); ++it) {
        MP4Info *mp4info = *it;
        TrackInfo *trackInfo = NULL;

        if (isWritingVideoTrack()) {
            trackInfo = mp4info->mVideoTrackInfo;
        }
        else {
            trackInfo = mp4info->mAudioTrackInfo;
        }

        int32_t stco0 = mp4info->mVideoTrackInfo->stco[0].chunkOffset;

        for (vector<stcoEntry>::iterator i = trackInfo->stco.begin(); i != trackInfo->stco.end(); ++i) {
            writeInt32(i->chunkOffset - stco0 + off);
        }

        off += mp4info->mdatSize - 8;
    }

    endBox();    
}

int32_t
MP4CatRewriter::getDuration()
{
    if (mDuration == -1) {
        mDuration = 0;
        for (list<MP4Info*>::iterator it = mCatTask->mInfoList.begin(); it != mCatTask->mInfoList.end(); ++it) {
            mDuration += (*it)->duration;
        }
    }

    return mDuration;
}

int64_t
MP4CatRewriter::getDurationUs()
{
    return getDuration() * 1E6 / getTimeScale();
}

int32_t
MP4CatRewriter::getTimeScale()
{
    return mCatTask->mInfoList.front()->timeScale;
}

int32_t
MP4CatRewriter::getTrackCount()
{
    return mCatTask->mInfoList.front()->trackCount;
}

int32_t
MP4CatRewriter::getVideoWidth()
{
    return mCatTask->mInfoList.front()->mVideoTrackInfo->_width;
}


int32_t
MP4CatRewriter::getVideoHeight()
{
    return mCatTask->mInfoList.front()->mVideoTrackInfo->_height;
}

int32_t
MP4CatRewriter::getAVCWidth()
{
    return mCatTask->mInfoList.front()->mVideoTrackInfo->avcWidth;
}

int32_t
MP4CatRewriter::getAVCHeight()
{
    return mCatTask->mInfoList.front()->mVideoTrackInfo->avcHeight;
}

void
MP4CatRewriter::getAVCCodecSpec(char **spec, int32_t *specLen)
{
    *spec = mCatTask->mInfoList.front()->mVideoTrackInfo->avcCodecSpec;
    *specLen = mCatTask->mInfoList.front()->mVideoTrackInfo->avcCodecSpecLen;
}

void
MP4CatRewriter::getAACCodecSpec(char **spec, int32_t *specLen)
{
    *spec = mCatTask->mInfoList.front()->mAudioTrackInfo->codecSpecData;
    *specLen = mCatTask->mInfoList.front()->mAudioTrackInfo->codecSpecDataLen;
}

int32_t
MP4CatRewriter::getTrackTimeScale()
{
    if (isWritingVideoTrack()) {
        return mCatTask->mInfoList.front()->mVideoTrackInfo->timeScale;
    }
    else {
        return mCatTask->mInfoList.front()->mAudioTrackInfo->timeScale;
    }
}


int32_t
MP4CatRewriter::getSampleCount()
{
    if (mVideoSampleCount == -1) {
        mVideoSampleCount = 0;
        mAudioSampleCount = 0;
        for (list<MP4Info*>::iterator it = mCatTask->mInfoList.begin(); it != mCatTask->mInfoList.end(); ++it) {
            mVideoSampleCount += (*it)->mVideoTrackInfo->stsz.size();
            mAudioSampleCount += (*it)->mAudioTrackInfo->stsz.size();
        }
    }

    return isWritingVideoTrack() ? mVideoSampleCount : mAudioSampleCount;
}


