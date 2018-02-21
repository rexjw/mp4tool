#include "mp4trimmer.h"
#include "mp4rewriter.h"


#include <algorithm>

using namespace std;

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

#define BE_16(x) ((((uint8_t*)(x))[0] <<  8) | ((uint8_t*)(x))[1])

#define BE_32(x) ((((uint8_t*)(x))[0] << 24) |  \
                  (((uint8_t*)(x))[1] << 16) |  \
                  (((uint8_t*)(x))[2] <<  8) |  \
                   ((uint8_t*)(x))[3])

#define BE_64(x) (((uint64_t)(((uint8_t*)(x))[0]) << 56) |  \
                  ((uint64_t)(((uint8_t*)(x))[1]) << 48) |  \
                  ((uint64_t)(((uint8_t*)(x))[2]) << 40) |  \
                  ((uint64_t)(((uint8_t*)(x))[3]) << 32) |  \
                  ((uint64_t)(((uint8_t*)(x))[4]) << 24) |  \
                  ((uint64_t)(((uint8_t*)(x))[5]) << 16) |  \
                  ((uint64_t)(((uint8_t*)(x))[6]) <<  8) |  \
                  ((uint64_t)( (uint8_t*)(x))[7]))

#define BE_FOURCC(ch0, ch1, ch2, ch3)           \
    ( (uint32_t)(unsigned char)(ch3)        |   \
     ((uint32_t)(unsigned char)(ch2) <<  8) |   \
     ((uint32_t)(unsigned char)(ch1) << 16) |   \
     ((uint32_t)(unsigned char)(ch0) << 24) )

#define QT_ATOM BE_FOURCC
/* top level atoms */
#define FREE_ATOM QT_ATOM('f', 'r', 'e', 'e')
#define JUNK_ATOM QT_ATOM('j', 'u', 'n', 'k')
#define MDAT_ATOM QT_ATOM('m', 'd', 'a', 't')
#define MOOV_ATOM QT_ATOM('m', 'o', 'o', 'v')
#define PNOT_ATOM QT_ATOM('p', 'n', 'o', 't')
#define SKIP_ATOM QT_ATOM('s', 'k', 'i', 'p')
#define WIDE_ATOM QT_ATOM('w', 'i', 'd', 'e')
#define PICT_ATOM QT_ATOM('P', 'I', 'C', 'T')
#define FTYP_ATOM QT_ATOM('f', 't', 'y', 'p')
#define UUID_ATOM QT_ATOM('u', 'u', 'i', 'd')

#define CMOV_ATOM QT_ATOM('c', 'm', 'o', 'v')
#define STCO_ATOM QT_ATOM('s', 't', 'c', 'o')
#define CO64_ATOM QT_ATOM('c', 'o', '6', '4')

#define MVHD_ATOM QT_ATOM('m', 'v', 'h', 'd')
#define TRAK_ATOM QT_ATOM('t', 'r', 'a', 'k')
#define TKHD_ATOM QT_ATOM('t', 'k', 'h', 'd')
#define MDIA_ATOM QT_ATOM('m', 'd', 'i', 'a')
#define MDHD_ATOM QT_ATOM('m', 'd', 'h', 'd')
#define HDLR_ATOM QT_ATOM('h', 'd', 'l', 'r')
#define MINF_ATOM QT_ATOM('m', 'i', 'n', 'f')
#define SMHD_ATOM QT_ATOM('s', 'm', 'h', 'd')
#define VMHD_ATOM QT_ATOM('v', 'm', 'h', 'd')
#define DINF_ATOM QT_ATOM('d', 'i', 'n', 'f')
#define DREF_ATOM QT_ATOM('d', 'r', 'e', 'f')
#define URL__ATOM QT_ATOM('u', 'r', 'l', ' ')

#define STBL_ATOM QT_ATOM('s', 't', 'b', 'l')
#define STSD_ATOM QT_ATOM('s', 't', 's', 'd')
#define AVC1_ATOM QT_ATOM('a', 'v', 'c', '1')
#define AVCC_ATOM QT_ATOM('a', 'v', 'c', 'C')
#define PASP_ATOM QT_ATOM('p', 'a', 's', 'p')
#define MP4A_ATOM QT_ATOM('m', 'p', '4', 'a')
#define ESDS_ATOM QT_ATOM('e', 's', 'd', 's')

#define STTS_ATOM QT_ATOM('s', 't', 't', 's')
#define CTTS_ATOM QT_ATOM('c', 't', 't', 's')
#define STSS_ATOM QT_ATOM('s', 't', 's', 's')
#define STSZ_ATOM QT_ATOM('s', 't', 's', 'z')
#define STSC_ATOM QT_ATOM('s', 't', 's', 'c')
#define STCO_ATOM QT_ATOM('s', 't', 'c', 'o')

#define VIDE_FOURCC BE_FOURCC('v', 'i', 'd', 'e')
#define SOUN_FOURCC BE_FOURCC('s', 'o', 'u', 'n')

#define ATOM_PREAMBLE_SIZE    8
#define COPY_BUFFER_SIZE      (256 * 1024)


bool compareTimeTableEntry(const TimeTableEntry& a, int32_t timestamp)
{
    return a.mTimestamp < timestamp;
}

int16_t read_int16(FILE *f)
{
    char buff[2];
    fread(buff, 2, 1, f);
    return BE_16(buff);
}

int32_t read_int32(FILE *f)
{
    char buff[4];
    fread(buff, 4, 1, f);
    return BE_32(buff);
}

void skipNBytes(FILE *imp4, int step)
{
    fseeko(imp4, step, SEEK_CUR);
}

MP4Info*
ExtractMP4Info(string filePath)
{
    MP4Info *mp4info = new MP4Info;
    mp4info->mFilePath = filePath;

    unsigned char atom_bytes[ATOM_PREAMBLE_SIZE];
    uint32_t atom_type   = 0;
    uint64_t atom_size   = 0;


    TrackInfo *ti = NULL;

    FILE *imp4 = fopen(filePath.c_str(), "rb");
    
    while (!feof(imp4)) {
        long position = ftell(imp4);

        if (fread(atom_bytes, ATOM_PREAMBLE_SIZE, 1, imp4) != 1) {
            break;
        }
        atom_size = (uint32_t) BE_32(&atom_bytes[0]);
        atom_type = BE_32(&atom_bytes[4]);

        bool case64 = false;
        if (atom_size == 1) { /* 64-bit special case */
            case64 = true;
            if (fread(atom_bytes, ATOM_PREAMBLE_SIZE, 1, imp4) != 1) {
                break;
            }
            atom_size = BE_64(&atom_bytes[0]);
        }

        if (atom_size < 8) {
            _I("go to hell! \n");
            return NULL;
        }

        //_I("read box: %c%c%c%c with size: %lld\n", atom_bytes[4], atom_bytes[5], atom_bytes[6], atom_bytes[7], atom_size);
        off_t step = atom_size - ATOM_PREAMBLE_SIZE * (case64 ? 2 : 1);

        bool doSeek = true;
        switch (atom_type)
        {
        case FTYP_ATOM:
            doSeek = true;
            break;

        case MDAT_ATOM:
            mp4info->mdatOffset = position;
            mp4info->mdatSize = atom_size;
            doSeek = true;
            break;

        case MOOV_ATOM:
            mp4info->moovOffset = position;
            mp4info->moovSize = atom_size;
            doSeek = false;
            break;

        case MVHD_ATOM:
        {
            int32_t _ = read_int32(imp4);
            (void)_;
            read_int32(imp4); // creationTime
            read_int32(imp4); // modificationTime
            mp4info->timeScale = read_int32(imp4);
            mp4info->duration = read_int32(imp4);
            skipNBytes(imp4, 76);
            mp4info->trackCount = read_int32(imp4);
            mp4info->trackCount--;
            doSeek = false;
            break;
        }

        case TRAK_ATOM:
            ti = new TrackInfo;
            doSeek = false;
            break;

        case TKHD_ATOM:
        {
            read_int32(imp4); // _
            read_int32(imp4); // creationTime
            read_int32(imp4); // modificationTime
            ti->trackID = read_int32(imp4); // trackID
            read_int32(imp4); // reserved
            ti->duration = read_int32(imp4); // duration
            _I("trackID:%d, duration:%d\n", ti->trackID, ti->duration);
            skipNBytes(imp4, atom_size - 10 * 4);
            ti->_width = read_int32(imp4); // _width
            ti->_height = read_int32(imp4); // _height
            doSeek = false;
            break;
        }

        case MDIA_ATOM:
            doSeek = false;
            break;

        case MDHD_ATOM:
        {
            read_int32(imp4); // _
            read_int32(imp4); // creationTime
            read_int32(imp4); // modificationTime
            ti->timeScale = read_int32(imp4); // timescale
            read_int32(imp4); // duration
            read_int32(imp4); // language
            doSeek = false;
            break;
        }

        case HDLR_ATOM:
        {
            read_int32(imp4); // _
            read_int32(imp4); // component type, should be mhlr
            int32_t handler = read_int32(imp4); // handler | component subtype
            if (handler == VIDE_FOURCC) {
                ti->mIsVideo = true;
                mp4info->mVideoTrackInfo = ti;
            }
            else if (handler == SOUN_FOURCC) {
                ti->mIsVideo = false;
                mp4info->mAudioTrackInfo = ti;
            }
            else {
                // TODO
                _I("go to hell");
                return NULL;
            }
            skipNBytes(imp4, atom_size - 20);
            doSeek = false;
            break;
        }

        case MINF_ATOM:
            doSeek = false;
            break;

        case SMHD_ATOM:
            break;

        case VMHD_ATOM:
        case DINF_ATOM:
        case DREF_ATOM:
        case URL__ATOM:
            break;

        case STBL_ATOM:
            doSeek = false;
            break;

        case STSD_ATOM:
            read_int32(imp4); // _
            read_int32(imp4); // entry count
            doSeek = false;
            break;

        case AVC1_ATOM:
        {
            // 32 reserved
            // 16 reserved
            // 16 data ref index
            // 16 predefined
            // 16 reserved
            // 32 predefined
            // 32 predefined
            // 32 predefined
            skipNBytes(imp4, 24);

            ti->avcWidth = read_int16(imp4);
            ti->avcHeight = read_int16(imp4);
            _I("avc-widht:%d, avc-height:%d | %llu\n", (int)ti->avcWidth, (int)ti->avcHeight, atom_size);


            // skipNBytes(imp4, atom_size - 8 - 24 - 4);
            // 32 horiz resolution
            // 32 vert resolution
            // 32 reserved
            // 16 frame count
            // 8 compressor string length
            // 31Bytes xxxx
            // 16 depth
            // 16 predefined
            skipNBytes(imp4, 50);

            doSeek = false;
            break;
        }

        case AVCC_ATOM:
        {
            ti->avcCodecSpecLen = atom_size - 8;
            ti->avcCodecSpec = new char[ti->avcCodecSpecLen];
            fread(ti->avcCodecSpec, ti->avcCodecSpecLen, 1, imp4);
            doSeek = false;
            break;
        }

        case PASP_ATOM:
            break;

        case MP4A_ATOM:
            ti->codecSpecDataLen = atom_size - 8;
            ti->codecSpecData = new char[ti->codecSpecDataLen];
            fread(ti->codecSpecData, ti->codecSpecDataLen, 1, imp4);
            doSeek = false;
            break;

        case STTS_ATOM:
        {
            int32_t _ = read_int32(imp4);
            (void)_;
            int32_t count = read_int32(imp4);
            _I("stts count: %d\n", count);
            for (int  i = 0; i < count; ++i) {
                sttsEntry entry;
                entry.count = read_int32(imp4);
                entry.delta = read_int32(imp4);
                ti->stts.push_back(entry);
                _I("    %d - %d\n", entry.count, entry.delta);
            }
            doSeek = false;
            break;
        }

        case CTTS_ATOM:
        {
            int32_t _ = read_int32(imp4);
            (void)_;
            int32_t count = read_int32(imp4);
            _I("ctts count: %d\n", count);
            for (int i = 0; i < count; ++i) {
                cttsEntry entry;
                entry.count = read_int32(imp4);
                entry.delta = read_int32(imp4);
                ti->ctts.push_back(entry);
                _I("    %d - %d\n", entry.count, entry.delta);
            }
            break;
        }

        case STSS_ATOM:
        {
            int32_t _ = read_int32(imp4);
            (void)_;
            int32_t count = read_int32(imp4);
            _I("stss: ");
            for (int i = 0; i < count; ++i) {
                int32_t keyFrameIndex = read_int32(imp4);
                ti->stss.push_back(keyFrameIndex);
                _I("%d, ", keyFrameIndex);
            }
            _I("\n\n");
            doSeek = false;
            break;
        }

        case STSZ_ATOM:
        {
            int32_t _0 = read_int32(imp4);
            (void)_0;
            int32_t _1 = read_int32(imp4);
            (void)_1;
            int32_t count = read_int32(imp4);
            _I("%d -- %d -- %d \n", _0, _1, count);
            for (int i = 0; i < count; ++i) {
                uint32_t sampleSize = read_int32(imp4);
                ti->stsz.push_back(sampleSize);
            }
            doSeek = false;
            break;
        }

        case STSC_ATOM:
        {
            int32_t _ = read_int32(imp4);
            (void)_;
            int32_t count = read_int32(imp4);
            for (int i = 0; i < count; ++i) {
                stscEntry entry;
                entry.firstChunkIndex = read_int32(imp4);
                entry.samplesPerChunk = read_int32(imp4);
                int32_t _ = read_int32(imp4);
                (void)_;
                ti->stsc.push_back(entry);
            }
            doSeek = false;
            break;
        }

        case STCO_ATOM:
        {
            int32_t _ = read_int32(imp4);
            (void)_;
            int32_t count = read_int32(imp4);
            for (int i = 0; i < count; ++i) {
                stcoEntry entry;
                entry.firstSampleIndex = -1;
                entry.chunkOffset = read_int32(imp4);
                ti->stco.push_back(entry);
            }
#if 0
            int32_t fakeOffset = mp4info->mdatOffset + mp4info->mdatSize - 8;
            _I("fake stco entry with size %d insert into %lu\n", fakeOffset, ti->stco.size() - 1);
            ti->stco.push_back(fakeOffset);
#endif
            doSeek = false;
            break;
        }

        default:
            _I("unknown box type %02x %02x %02x %02x\n", atom_bytes[4], atom_bytes[5], atom_bytes[6], atom_bytes[7]);
            break;
        }

        if (doSeek == true) {
            fseeko(imp4, step, SEEK_CUR);
        }
    }
    return mp4info;
}

static int
BuildTimeTable(MP4Info *mp4info)
{
    TrackInfo *ti = mp4info->mVideoTrackInfo;

#if 0
    {
        // fake entry
        TimeTableEntry entry;
        entry.mID = 0;
        entry.mTimestamp = 0;
        entry.mIsKeyFrame = false;
        ti->mTimeTable.push_back(entry);
    }
#endif

    vector<int32_t>::iterator nextKeyFrameIDIterator = ti->stss.begin();
    int32_t nextKeyFrameID = *nextKeyFrameIDIterator;

    uint32_t id = 0;
    uint64_t timestamp = 0;
    for (vector<sttsEntry>::iterator it = ti->stts.begin(); it != ti->stts.end(); ++it) {
        for (int i = 0; i < it->count; ++i) {
            TimeTableEntry entry;
            entry.mID = ++id;
            entry.mTimestamp = timestamp;
            timestamp += it->delta;
            entry.mIsKeyFrame = false;
            if (entry.mID == nextKeyFrameID) {
                entry.mIsKeyFrame = true;
                if (++nextKeyFrameIDIterator != ti->stss.end()) {
                    nextKeyFrameID = *nextKeyFrameIDIterator;
                }
            }
            
            ti->mTimeTable.push_back(entry);
        }
    }

    {
        // fake entry
        TimeTableEntry fakeEntry;
        fakeEntry.mID = ++id;
#if 1
        fakeEntry.mTimestamp = timestamp;
#else
        fakeEntry.mTimestamp = numeric_limits<uint64_t>::max();
#endif
        fakeEntry.mIsKeyFrame = false;
        ti->mTimeTable.push_back(fakeEntry);
    }

    return 0;
}

static int
PerformTrim(MP4Info *mp4info, const char* dest)
{
    MP4Rewriter writer;
    writer.setOutputPath(dest);
    writer.open();
    writer.write(mp4info);
    writer.close();

    return 0;
}

int mp4trim(const char* src, const char* dest, int beginMs, int ceaseMs)
{
    if (beginMs < 0) {
        beginMs = 0;
    }

    if (ceaseMs < 0) {
        ceaseMs = -1;
    }

    MP4Info *mp4info = ExtractMP4Info(src);
    _I("mp4 duration: %dms, GOPs:%lu \n", mp4info->duration, mp4info->mVideoTrackInfo->stss.size());


    stcoEntry fakeStcoEntry;
    fakeStcoEntry.firstSampleIndex = -1;
    fakeStcoEntry.chunkOffset = mp4info->mdatOffset + mp4info->mdatSize;
    _I("fake stco entry with size %d\n", fakeStcoEntry.chunkOffset);
    mp4info->mVideoTrackInfo->stco.push_back(fakeStcoEntry);
    mp4info->mAudioTrackInfo->stco.push_back(fakeStcoEntry);

    _I("video stco -- %ld \n", mp4info->mVideoTrackInfo->stco.size());
    _I("audio stco -- %ld \n", mp4info->mAudioTrackInfo->stco.size());

    {
        auto it = mp4info->mVideoTrackInfo->stsc.begin();
        if (it == mp4info->mVideoTrackInfo->stsc.end()) {
            _E("video track has no stsc info!\n");
            return -9527;
        }

        int c = 0;
        int i = 0;
        while (it != mp4info->mVideoTrackInfo->stsc.end()) {
            auto itn = it + 1;
            int cc = mp4info->mVideoTrackInfo->stco.size();
            if (itn != mp4info->mVideoTrackInfo->stsc.end()) {
                cc = itn->firstChunkIndex - 1;
                // TODO if stco.size() < itn->firstChunkIndex
            }
            while (c < cc) {
                mp4info->mVideoTrackInfo->stco[c++].firstSampleIndex = i;
                i += it->samplesPerChunk;
            }
            it = itn;
        }
    }

    {
        auto it = mp4info->mAudioTrackInfo->stsc.begin();
        if (it == mp4info->mAudioTrackInfo->stsc.end()) {
            _E("audio track has no stsc info!\n");
            return -9528;
        }

        int c = 0;
        int i = 0;
        while (it != mp4info->mAudioTrackInfo->stsc.end()) {
            auto itn = it + 1;
            int cc = mp4info->mAudioTrackInfo->stco.size() - 1;
            if (itn != mp4info->mAudioTrackInfo->stsc.end()) {
                cc = itn->firstChunkIndex - 1;
                // TODO if stco.size() < itn->firstChunkIndex
            }
            while (c < cc) {
                mp4info->mAudioTrackInfo->stco[c++].firstSampleIndex = i;
                i += it->samplesPerChunk;
            }
            it = itn;
        }
    }


    BuildTimeTable(mp4info);
    _I("time table entry count: %lu \n", mp4info->mVideoTrackInfo->mTimeTable.size());

    _I("time scale: %d \n", mp4info->mVideoTrackInfo->timeScale);


    TrackInfo* videoInfo = mp4info->mVideoTrackInfo;


    // begin = beginMs / 1000 * 90000
    uint64_t begin = beginMs * (videoInfo->timeScale / 1000);
    vector<TimeTableEntry>::iterator beginIt = lower_bound(
                                    videoInfo->mTimeTable.begin(),
                                    videoInfo->mTimeTable.end(),
                                    begin, compareTimeTableEntry);
    _I("begin: %d|%llu -- %d -- %llu -- %s\n",beginMs, begin, beginIt->mID, beginIt->mTimestamp, (beginIt->mIsKeyFrame ? "true" : "false"));
    mp4info->trimBeginID0 = beginIt->mID;


    // cease = ceaseMs / 1000 * 90000
    if (ceaseMs != -1) {
        uint64_t cease = ceaseMs * (videoInfo->timeScale / 1000);
        vector<TimeTableEntry>::iterator ceaseIt = lower_bound(
                                        videoInfo->mTimeTable.begin(),
                                        videoInfo->mTimeTable.end(),
                                        cease, compareTimeTableEntry);
        if (ceaseIt == videoInfo->mTimeTable.end()) {
            --ceaseIt;
        }
        _I("cease: %d|%llu -- %d -- %llu -- %s\n", ceaseMs, cease, ceaseIt->mID, ceaseIt->mTimestamp, (ceaseIt->mIsKeyFrame ? "true" : "false"));
        mp4info->trimCeaseID0 = ceaseIt->mID;
    }
    else {
        mp4info->trimCeaseID0 = -1;
    }


    mp4info->trimBeginVideoID = mp4info->trimBeginID0;
    for (; mp4info->trimBeginVideoID && !videoInfo->mTimeTable[mp4info->trimBeginVideoID - 1].mIsKeyFrame; --mp4info->trimBeginVideoID)
    {}

    if (mp4info->trimCeaseID0 != -1) {
        mp4info->trimCeaseVideoID = mp4info->trimCeaseID0;
    }
    else {
        mp4info->trimCeaseVideoID = videoInfo->mTimeTable.size() - 1;
    }


    // TODO HEERE

    stcoVectorIterator cb = upper_bound(
                mp4info->mVideoTrackInfo->stco.begin(),
                mp4info->mVideoTrackInfo->stco.end(),
                mp4info->trimBeginVideoID - 1,
                compareStcoSampleIndex);
    --cb;
    stcoVectorIterator ce = upper_bound(
                mp4info->mVideoTrackInfo->stco.begin(),
                mp4info->mVideoTrackInfo->stco.end(),
                mp4info->trimCeaseVideoID - 1,
                compareStcoSampleIndex);
    --ce;

    mp4info->trimBeginVideoChunk = cb;
    mp4info->trimCeaseVideoChunk = ce;

    _I("cb: %d, %d, %d \n", mp4info->trimBeginVideoID, cb->firstSampleIndex, cb->chunkOffset);
    _I("ce: %d, %d, %d \n", mp4info->trimCeaseVideoID, ce->firstSampleIndex, ce->chunkOffset);

    mp4info->trimBeginOffset = cb->chunkOffset;
    for (auto i = cb->firstSampleIndex; i < mp4info->trimBeginVideoID - 1; ++i) {
        mp4info->trimBeginOffset += mp4info->mVideoTrackInfo->stsz[i];
    }

    mp4info->trimCeaseOffset = ce->chunkOffset;
    for (auto i = ce->firstSampleIndex; i < mp4info->trimCeaseVideoID - 1; ++i) {
        mp4info->trimCeaseOffset += mp4info->mVideoTrackInfo->stsz[i];
    }

    _I("media data: %lld --> %lld \n", mp4info->trimBeginOffset, mp4info->trimCeaseOffset);

    uint64_t timestampDelta = videoInfo->mTimeTable[mp4info->trimCeaseVideoID - 1].mTimestamp - videoInfo->mTimeTable[mp4info->trimBeginVideoID - 1].mTimestamp;
    mp4info->postTrimDurationUs = timestampDelta * 1000000 / videoInfo->timeScale;
    mp4info->postTrimDuration = (mp4info->postTrimDurationUs * mp4info->timeScale + 5E5) / 1E6;


    // audio stuffs
    stcoVectorIterator ab = upper_bound(
                mp4info->mAudioTrackInfo->stco.begin(),
                mp4info->mAudioTrackInfo->stco.end(),
                mp4info->trimBeginOffset,
                compareStcoOffset);
    stcoVectorIterator ac = upper_bound(
                mp4info->mAudioTrackInfo->stco.begin(),
                mp4info->mAudioTrackInfo->stco.end(),
                mp4info->trimCeaseOffset,
                compareStcoOffset);

    mp4info->trimBeginAudioChunk = ab;
    mp4info->trimCeaseAudioChunk = ac;

    mp4info->trimBeginAudioID = ab->firstSampleIndex + 1;
    mp4info->trimCeaseAudioID = ac->firstSampleIndex + 1;

    _I("audio begin: [%u] %d \n", mp4info->trimBeginAudioID, ab->chunkOffset);
    _I("audio cease: [%u] %d \n", mp4info->trimCeaseAudioID, ac->chunkOffset);

    // trim
    PerformTrim(mp4info, dest);

    return 0; 
}

int mp4cat(const list<string> & src, const string dest)
{
    if (find(src.begin(), src.end(), dest) != src.end()) {
        _E("Destination is one of the source!");
        return -1;
    }

    CatTask catTask;
    for (list<string>::const_iterator it = src.begin(); it != src.end(); ++it) {
        MP4Info *mp4info = ExtractMP4Info(*it);
        catTask.mInfoList.push_back(mp4info);
    }

    MP4CatRewriter writer;
    writer.setOutputPath(dest);
    writer.open();
    writer.write(&catTask);
    writer.close();

    return 0;
}

