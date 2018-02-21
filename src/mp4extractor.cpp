#include "mp4extractor.h"

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

using namespace std;

MP4Extractor::~MP4Extractor()
{}

class RealMP4Extractor : public MP4Extractor {
public:
	explicit RealMP4Extractor(string filePath);
	~RealMP4Extractor();

	virtual bool seek(int ms) override;

	virtual bool getCodecSpec(void **data, int32_t *len) override;
	virtual void releaseCodecSpec(void **data) override;

	virtual int32_t mediaDurationMs() const override { return mMediaDurationMs; }

//	virtual bool getPreviousIFrame(void **data, int32_t *len);
	virtual bool getNextFrame(void **data, int32_t *len) override;
	virtual void releaseFrame(void **data) override;

	virtual int32_t videoFrameWidth() const override { return mVideoWidth; }
	virtual int32_t videoFrameHeight() const  override { return mVideoHeight; }
	virtual int32_t videoFrameDurationUs() const override { return mSampleDurationUs; }

	virtual int32_t totalFrameCount() const  override { return mTotalFrameCount; }
	virtual int32_t accessableFrameCount() const  override { return mAccessableFrameCount; }
	virtual int32_t remainedFrameCount() const  override { return mRemainedFrameCount; }

public:
	bool prepare();

private:
	char* readData(int32_t offset, int32_t len);

private:
	string mFilePath;

	MP4Info *mInfo;

	int mMediaDurationMs;

	TrackInfo *mVideoTrackInfo;

	FILE *mFile;

	int mVideoWidth;
	int mVideoHeight;

	int mTotalFrameCount;
	int mAccessableFrameCount;
	int mRemainedFrameCount;

	int mSampleDurationUs;

	int mAnchorMs;
	int mAnchorCursor;
	int mPreviousIFrameCursor;

	int mCurrentCursor;
};

RealMP4Extractor::RealMP4Extractor(string filePath)
		: mFilePath(filePath)
		, mInfo(nullptr)
		, mMediaDurationMs(0)
		, mVideoTrackInfo(nullptr)
		, mFile(nullptr)
		, mTotalFrameCount(0)
		, mAccessableFrameCount(0)
		, mRemainedFrameCount(0)
		, mSampleDurationUs(0)
		, mAnchorMs(-1)
		, mAnchorCursor(-1)
		, mPreviousIFrameCursor(-1)
		, mCurrentCursor(-1)
{
}

RealMP4Extractor::~RealMP4Extractor()
{
	if (mFile != nullptr) {
		::fclose(mFile);
	}

	if (mInfo != 0) {
		delete mInfo;
	}
}

bool
RealMP4Extractor::prepare()
{
	mInfo = ExtractMP4Info(mFilePath);
	if (mInfo == nullptr) {
		_W("read mp4info failed! %s", mFilePath.c_str());
		return false;
	}

	mMediaDurationMs = mInfo->duration;

	mVideoTrackInfo = mInfo->mVideoTrackInfo;
	if (mVideoTrackInfo == nullptr) {
		_W("no video track found! %s", mFilePath.c_str());
		return false;
	}
	
	mFile = ::fopen(mFilePath.c_str(), "rb");
	if (mFile == nullptr) {
		_W("open file failed! %s", mFilePath.c_str());
		return false;
	}

	mVideoWidth = mVideoTrackInfo->avcWidth;
	mVideoHeight = mVideoTrackInfo->avcHeight;
	_I("frame matrix: %d * %d\n", mVideoWidth, mVideoHeight);

	mTotalFrameCount = mVideoTrackInfo->stsz.size();
	_I("total frame count: %d \n", mTotalFrameCount);

	long delta = mVideoTrackInfo->stts[0].delta;
	delta = delta * 1000000 / mVideoTrackInfo->timeScale;

	mSampleDurationUs = delta; //mVideoTrackInfo->stts[0].delta;
	_I("sample duration is %d \n", mSampleDurationUs);

	seek(0);

	return true;
}

bool
RealMP4Extractor::seek(int ms)
{
	if (ms < 0) {
		ms = 0;
	}

	mAnchorMs = ms;
	_I("seek to %dms \n", ms);

	mAnchorCursor = ms * 1000 / mSampleDurationUs;

	if (mAnchorCursor >= mTotalFrameCount) {
		mAnchorCursor = mTotalFrameCount - 1;
	}

	mPreviousIFrameCursor = mAnchorCursor + 1;
	for (int i = 1; i < mVideoTrackInfo->stss.size(); ++i) {
		if (mVideoTrackInfo->stss[i - 1] <= mPreviousIFrameCursor &&
				mPreviousIFrameCursor < mVideoTrackInfo->stss[i]) {
			mPreviousIFrameCursor = mVideoTrackInfo->stss[i - 1];
			break;
		}
	}
	mPreviousIFrameCursor--;

	mAnchorCursor = mPreviousIFrameCursor;

	mCurrentCursor = mAnchorCursor;

	_I("anchors: %d - %d - %d \n", mPreviousIFrameCursor, mAnchorCursor, mCurrentCursor);

	mAccessableFrameCount = mTotalFrameCount - mAnchorCursor;
	mRemainedFrameCount = mAccessableFrameCount;
	_I("frame count: %d - %d - %d \n", mTotalFrameCount, mAccessableFrameCount, mRemainedFrameCount);

	// TODO

	return false;
}

char*
RealMP4Extractor::readData(int32_t offset, int32_t len)
{
	// TODO

	::fseek(mFile, offset, SEEK_SET);

	char* buff = new char[len];

	::fread(buff, len, 1, mFile);

	return buff;
}

void
RealMP4Extractor::releaseFrame(void **data)
{
	char *buff = (char*)*data;
	delete [] buff;

	*data = nullptr;
}

bool
RealMP4Extractor::getCodecSpec(void **data, int32_t *len)
{
	*data = mVideoTrackInfo->avcCodecSpec;
	*len = mVideoTrackInfo->avcCodecSpecLen;

	return true;
}

void
RealMP4Extractor::releaseCodecSpec(void **data)
{
	*data = nullptr;
}

#if 0
bool
RealMP4Extractor::getPreviousIFrame(void **data, int32_t *len)
{
	*data = nullptr;
	*len = 0;
	if (mPreviousIFrameCursor >= 0) {
		if (mPreviousIFrameCursor < mAnchorCursor) {
			int32_t offset = mVideoTrackInfo->stco[mPreviousIFrameCursor];
			*len = mVideoTrackInfo->stsz[mPreviousIFrameCursor];
			*data = readData(offset, *len);
		}
		return true;
	}
	else {
		return false;
	}
}
#endif

bool
RealMP4Extractor::getNextFrame(void **data, int32_t *len)
{
	*data = nullptr;
	*len = 0;

	if (mCurrentCursor == -1 || mRemainedFrameCount == 0) {
		return false;
	}

	int32_t offset = mVideoTrackInfo->stco[mCurrentCursor].chunkOffset; // FIXME
	*len = mVideoTrackInfo->stsz[mCurrentCursor];
	*data = readData(offset, *len);

	++mCurrentCursor;
	--mRemainedFrameCount;

	return true;
}

MP4Extractor*
createMP4Extractor(string filePath)
{
	RealMP4Extractor* extractor = new RealMP4Extractor(filePath);
	if (extractor->prepare()) {
		return extractor;
	}
	else {
		delete extractor;
		return nullptr;
	}
}

void
destroyMP4Extractor(MP4Extractor* extractor)
{
	delete extractor;
}

