#ifndef MP4_REWRITER_H
#define MP4_REWRITER_H

#include <sys/types.h>

#include <string>
#include <list>

#include "mp4trimmer.h"

struct MP4Info;
struct TrackInfo;

class MP4Rewriter
{
public:
	MP4Rewriter();
	virtual ~MP4Rewriter();

	int setOutputPath(const std::string path);

	int open();
	int close();

	int write(MP4Info *mp4info);

protected:

	int copyData(int srcFD, off_t posi, int size, const std::string srcName);

	size_t write(const void* data, size_t size, size_t nmemb);

	void write(const void* data, size_t size);
	void writeInt8(int8_t);
	void writeInt16(int16_t);
	void writeInt32(int32_t);
	void writeInt64(int64_t);
	void writeFourcc(const char*);
	void writeCString(const char*);

	struct BoxInfo
	{
		off_t offset;
		const char* name;
	};
	std::list<BoxInfo> mBoxes;


	void beginBox(const char* fourcc);
	void endBox();

	void writeFtypBox();
	void writeMoovBox();
	void writeMvhdBox();
	void writeCompositionMatrix(int degress);

	void writeTrackInfo();
	void writeTkhdBox(uint32_t now);
	void writeMdhdBox(uint32_t now);
	void writeHdlrBox();
	void writeVmhdBox();
	void writeSmhdBox();
	void writeDinfBox();

	int writeStblBox();
	void writeVideoFourCCBox();
	void writeAudioFourCCBox();

	virtual void writeStssBox();
	virtual void writeStszBox();
	virtual void writeStcoBox();

protected:
	void beginVideoTrack() { mIsWritingVideoTrack = true; mIsWritingAudioTrack = false; }
	void beginAudioTrack() { mIsWritingVideoTrack = false; mIsWritingAudioTrack = true; }

	bool isWritingVideoTrack() const { return mIsWritingVideoTrack; }

	int32_t getTrackID() { return mIsWritingVideoTrack ? 1 : 2; }

	virtual int32_t getDuration() { return mMP4Info->postTrimDuration; }
	virtual int64_t getDurationUs() { return mMP4Info->postTrimDurationUs; }
	virtual int32_t getTimeScale() { return mMP4Info->timeScale; }
	virtual int32_t getTrackCount() { return mMP4Info->trackCount; }
	virtual int32_t getVideoWidth() { return mMP4Info->mVideoTrackInfo->_width; }
	virtual int32_t getVideoHeight() { return mMP4Info->mVideoTrackInfo->_height; }

	virtual int32_t getAVCWidth() { return mMP4Info->mVideoTrackInfo->avcWidth; }
	virtual int32_t getAVCHeight() { return mMP4Info->mVideoTrackInfo->avcHeight; }

	virtual void getAVCCodecSpec(char **spec, int32_t *specLen)
	{
		*spec = mMP4Info->mVideoTrackInfo->avcCodecSpec;
		*specLen = mMP4Info->mVideoTrackInfo->avcCodecSpecLen;
	}

	virtual void getAACCodecSpec(char **spec, int32_t *specLen)
	{
		*spec = mMP4Info->mAudioTrackInfo->codecSpecData;
		*specLen = mMP4Info->mAudioTrackInfo->codecSpecDataLen;
	}

	virtual int32_t getTrackTimeScale()
	{
		if (mIsWritingVideoTrack) {
			return mMP4Info->mVideoTrackInfo->timeScale;
		}
		else {
			return mMP4Info->mAudioTrackInfo->timeScale;
		}
	}

	virtual int32_t getSampleCount()
	{
		if (mIsWritingVideoTrack) {
			return mMP4Info->trimCeaseVideoID - mMP4Info->trimBeginVideoID;
		}
		else {
			return mMP4Info->trimCeaseAudioID - mMP4Info->trimBeginAudioID;
		}
	}

	virtual int32_t getSampleDelta()
	{
		return mIsWritingVideoTrack ? 5000 : 1024; // so far so good // TODO
	}

private:
	std::string mPath;

	int mFD;

	off_t mOffset;

	bool mIsWritingVideoTrack;
	bool mIsWritingAudioTrack;

	MP4Info *mMP4Info;

	// TrackInfo *mTrackInfo;
	// TODO TrackInfoProvider *mTrackInfo; a solution better than mIsWritingVideoTrack
};

struct CatTask;
class MP4CatRewriter : public MP4Rewriter
{
public:
    MP4CatRewriter();
    ~MP4CatRewriter();

    int write(CatTask *);

protected:
    virtual void writeStssBox() override;
    virtual void writeStszBox() override;
    virtual void writeStcoBox() override;

    virtual int32_t getDuration() override;
    virtual int64_t getDurationUs() override;
    virtual int32_t getTimeScale() override;
    virtual int32_t getTrackCount() override;
    virtual int32_t getVideoWidth() override;
    virtual int32_t getVideoHeight() override;
    virtual int32_t getAVCWidth() override;
    virtual int32_t getAVCHeight() override;
    virtual void getAVCCodecSpec(char **sepc, int32_t *specLen) override;
    virtual void getAACCodecSpec(char **spec, int32_t *specLen) override;
    virtual int32_t getTrackTimeScale() override;
    virtual int32_t getSampleCount() override;
    // virtual int32_t getSampleDelte();

private:
    CatTask     *mCatTask;

    int32_t     mDuration;
    int32_t     mVideoSampleCount;
    int32_t     mAudioSampleCount;
};

#endif // MP4_REWRITER_H

