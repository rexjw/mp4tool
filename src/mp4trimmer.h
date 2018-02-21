#ifndef MP4_TRIMMER_H
#define MP4_TRIMMER_H

#include <vector>
#include <list>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

struct sttsEntry
{
    int32_t count;
    int32_t delta;
};

struct cttsEntry
{
    int32_t count;
    int32_t delta;
};

struct stscEntry
{
    int32_t firstChunkIndex;
    int32_t samplesPerChunk;
};

inline bool compareStscChunkIndex(int32_t chunkIndex, const stscEntry& entry)
{
    return chunkIndex < entry.firstChunkIndex;
}

struct stcoEntry
{
    int32_t firstSampleIndex;
    int32_t chunkOffset;
};

typedef std::vector<stcoEntry> stcoVector;
typedef stcoVector::iterator stcoVectorIterator;

inline bool compareStcoSampleIndex(int32_t sampleIndex, const stcoEntry& entry)
{
    return sampleIndex < entry.firstSampleIndex;
}

inline bool compareStcoOffset(int32_t chunkOffset, const stcoEntry& entry)
{
    return chunkOffset < entry.chunkOffset;
}

struct TimeTableEntry
{
    uint32_t mID;
    uint64_t mTimestamp;
    bool mIsKeyFrame;
};

struct TrackInfo
{
    bool mIsVideo;

    int32_t trackID;
    int32_t duration;
    int32_t _width;
    int32_t _height;

    int32_t timeScale; // mdhd

    int16_t avcWidth;
    int16_t avcHeight;
    char *avcCodecSpec;
    int32_t avcCodecSpecLen;

    char *codecSpecData;
    int32_t codecSpecDataLen;

    std::vector<sttsEntry> stts; // sample duration
    std::vector<cttsEntry> ctts; //
    std::vector<int32_t> stss; // key-frame index
    std::vector<int32_t> stsz; // sample size

    std::vector<stscEntry> stsc; // sample count per chunk
    std::vector<stcoEntry> stco; // chunk offset

    std::vector<TimeTableEntry> mTimeTable;
};

struct MP4Info
{
    std::string mFilePath;

    long mdatOffset;
    uint64_t mdatSize;

    long moovOffset;
    uint64_t moovSize;

    int32_t timeScale;
    int32_t duration;

    // char compositionMatrix[36];

    int32_t trackCount;

    TrackInfo *mVideoTrackInfo;
    TrackInfo *mAudioTrackInfo;

    int32_t trimBeginID0;
    int32_t trimCeaseID0;

    int32_t trimBeginVideoID;
    int32_t trimCeaseVideoID;

    stcoVectorIterator trimBeginVideoChunk;
    stcoVectorIterator trimCeaseVideoChunk;

    int32_t trimBeginAudioID;
    int32_t trimCeaseAudioID;

    stcoVectorIterator trimBeginAudioChunk;
    stcoVectorIterator trimCeaseAudioChunk;

    off_t trimBeginOffset;
    off_t trimCeaseOffset;

    int64_t postTrimDurationUs;
    int32_t postTrimDuration;

    int32_t postTrimMediaDataOffset;

    int32_t postTrimFirstVideoOffset;
    int32_t postTrimFirstAudioOffset;
};

struct TrimTask
{
    // TODO
    int _;
};

struct CatTask
{
    std::list<MP4Info*> mInfoList;
};

MP4Info* ExtractMP4Info(std::string filePath);

int mp4trim(const char* src, const char* dest, int beginMs, int ceaseMs);
int mp4cat(const std::list<std::string> & src, const std::string dest);

#ifdef __cplusplus
}
#endif

#endif // MP4_TRIMMER_H
