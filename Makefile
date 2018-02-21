##

all:
	g++ -std=c++11 -g -Wall -o mp4tool \
		src/mp4trimmer.cpp \
		src/mp4rewriter.cpp \
		src/mp4extractor.cpp \
		src/main.cpp

clean:
	rm -rf mp4tool
	rm -rf mp4tool.dSYM

