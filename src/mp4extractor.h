#ifndef MP4_EXTRACTOR_H
#define MP4_EXTRACTOR_H

#include <string>

#include "cppdef.h"

class MP4Extractor {
public:
	virtual ~MP4Extractor() = 0;

	virtual bool seek(int ms) = 0; // TODO SEEK_MODE: prev, next, near

	virtual bool getCodecSpec(void **data, int32_t *len) = 0;
	virtual void releaseCodecSpec(void **data) = 0;

	virtual int32_t mediaDurationMs() const = 0;

//	virtual bool getPreviousIFrame(void **data, int32_t *len) = 0;

	virtual bool getNextFrame(void **data, int32_t *len) = 0;
	virtual void releaseFrame(void **data) = 0;

	virtual int32_t videoFrameWidth() const = 0;
	virtual int32_t videoFrameHeight() const = 0;
	virtual int32_t videoFrameDurationUs() const = 0; // XXX

	virtual int32_t totalFrameCount() const = 0;
	virtual int32_t accessableFrameCount() const = 0;
	virtual int32_t remainedFrameCount() const = 0;
};

MP4Extractor* createMP4Extractor(std::string filePath);
void destroyMP4Extractor(MP4Extractor*);

#endif // MP4_EXTRACTOR_H

