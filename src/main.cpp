#include "mp4trimmer.h"
#include "mp4extractor.h"

#ifdef __APPLE__

using namespace std;

#define _I printf

void cat()
{
	list<string> srcList;
	srcList.push_back("b.mp4");
	srcList.push_back("a.mp4");
	srcList.push_back("b.mp4");
	srcList.push_back("b.mp4");
	srcList.push_back("b.mp4");
	srcList.push_back("b.mp4");
	srcList.push_back("b.mp4");

	mp4cat(srcList, "c.mp4");
}

void extract()
{
	MP4Extractor *extractor = createMP4Extractor("./a.mp4");

	bool ret = false;
	void *data = NULL;
	int32_t len = 0;


	ret = extractor->getCodecSpec(&data, &len);
	_I("codec spec: ret:%d, len:%d \n", ret, len);

	_I("===========================");
	extractor->seek(6000);
#if 0
	ret = extractor->getPreviousIFrame(&data, &len);
	extractor->releaseFrame(&data);
	_I("ret:%d, len:%d \n", ret, len);
#endif
	for (int i = 0; i < 20; ++i) {
		ret = extractor->getNextFrame(&data, &len);
		extractor->releaseFrame(&data);
		_I("ret:%d, len:%d, remained:%d \n", ret, len, extractor->remainedFrameCount());
	}

	_I("===========================");
	extractor->seek(100);
	for (int i = 0; i < 20; ++i) {
		ret = extractor->getNextFrame(&data, &len);
		extractor->releaseFrame(&data);
		_I("ret:%d, len:%d, remained:%d \n", ret, len, extractor->remainedFrameCount());
	}

	_I("===========================");
	extractor->seek(100000);
	for (int i = 0; i < 20; ++i) {
		ret = extractor->getNextFrame(&data, &len);
		extractor->releaseFrame(&data);
		_I("ret:%d, len:%d, remained:%d \n", ret, len, extractor->remainedFrameCount());
	}

	destroyMP4Extractor(extractor);
}

int main(int argc, char **argv)
{
	mp4trim("./src.mp4", "./dst.mp4", 0, 10000);
	// mp4trim("./gie.mp4", "./aut.mp4", 0, 10000);
	// extract();

	return 0;
}

#endif

