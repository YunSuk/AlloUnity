#include <vector>
#include <unordered_map>
#include "H264CubemapSource.h"

void H264CubemapSource::setOnReceivedNALU(const OnReceivedNALU& callback)
{
    onReceivedNALU = callback;
}

void H264CubemapSource::setOnReceivedFrame(const OnReceivedFrame& callback)
{
    onReceivedFrame = callback;
}

void H264CubemapSource::setOnDecodedFrame(const OnDecodedFrame& callback)
{
    onDecodedFrame = callback;
}

void H264CubemapSource::setOnColorConvertedFrame(const OnColorConvertedFrame& callback)
{
    onColorConvertedFrame = callback;
}

void H264CubemapSource::setOnNextCubemap(const OnNextCubemap& callback)
{
    onNextCubemap = callback;
}

void H264CubemapSource::getNextFramesLoop()
{
	std::vector<AVFrame*> frames(sinks.size(), nullptr);
	
    int64_t lastPTS = 0;
    while (true)
    {
        // Get all the decoded frames
        for (int i = 0; i < sinks.size(); i++)
        {
            
			frames[i] = sinks[i]->getNextFrame();
            
            
            
            if (frames[i])
            {
                //std::cout << "PTS diff " << frames[i]->pts - lastPTS << std::endl;
                lastPTS = frames[i]->pts;
                
                boost::mutex::scoped_lock lock(frameMapMutex);
                
                //
                if (std::abs(lastDisplayPTS - frames[i]->pts) < 3000 || frames[i]->pts < lastDisplayPTS)
                {
                    std::cout << "frame comes too late" << std::endl;
                    sinks[i]->returnFrame(frames[i]);
                    continue;
                }
                
                // The pts get changed a little by live555.
                // Find a pts that is close enough so that we can put it in the container
                // with the right other faces
                uint64_t key = 0;
                for (auto it = frameMap.begin(); it != frameMap.end(); ++it)
                {
                    if (std::abs(it->first - frames[i]->pts) < 3000)
                    {
                        key = it->first;
                        break;
                    }
                }
                
                if (key == 0)
                {
                    key = frames[i]->pts;
                    frameMap[key].resize(sinks.size());
                }
                
                std::vector<AVFrame*>& bucketFrames = frameMap[key];
                if (bucketFrames[i])
                {
                    // Matches should not happen here.
                    // If it happens give back frame immediately
                    //std::cout << "match!? (" << i << ")" << bucketFrames[i]->pts << " " << frames[i]->pts << std::endl;
                    sinks[i]->returnFrame(frames[i]);
                }
                else
                {
                    bucketFrames[i] = frames[i];
                    //std::cout << "matched bucket " << frames[i] << " " << key << " " << frames[i]->pts << std::endl;
                }
            }
        }
        //std::cout << frameMap.size() << std::endl;
    }
}

void H264CubemapSource::getNextCubemapLoop()
{
    int64_t lastPTS = 0;
    boost::chrono::system_clock::time_point lastDisplayTime(boost::chrono::microseconds(0));
    
    while (true)
    {
        uint64_t pts;
        size_t pendingCubemaps;
        // Get frames with the oldest pts and remove the associated bucket
        std::vector<AVFrame*> frames;
        {
            boost::mutex::scoped_lock lock(frameMapMutex);
            
            if (frameMap.size() < 10)
            {
                continue;
            }
            pendingCubemaps = frameMap.size();
            
            std::map<int64_t, std::vector<AVFrame*>>::iterator it = frameMap.begin();
            pts = it->first;
            lastDisplayPTS = it->first;
            frames = it->second;
            frameMap.erase(it);
        }
        
        StereoCubemap* cubemap;
        
        // Allocate cubemap if necessary
        if (!oldCubemap)
        {
            int width, height;
            for (AVFrame* frame : frames)
            {
                if (frame)
                {
                    width  = frame->width;
                    height = frame->height;
                    break;
                }
            }
            
            std::vector<Cubemap*> eyes;
            for (int j = 0, faceIndex = 0; j < StereoCubemap::MAX_EYES_COUNT && faceIndex < sinks.size(); j++)
            {
                std::vector<CubemapFace*> faces;
                for (int i = 0; i < Cubemap::MAX_FACES_COUNT && faceIndex < sinks.size(); i++, faceIndex++)
                {
                    Frame* content = Frame::create(width,
                                                   height,
                                                   format,
                                                   boost::chrono::system_clock::time_point(),
                                                   heapAllocator);
                    
                    CubemapFace* face = CubemapFace::create(content,
                                                            i,
                                                            heapAllocator);
                    
                    faces.push_back(face);
                }
                
                eyes.push_back(Cubemap::create(faces, heapAllocator));
            }
            
            cubemap = StereoCubemap::create(eyes, heapAllocator);
        }
        else
        {
            cubemap = oldCubemap;
        }
        
        size_t count = 0;
        // Fill cubemap making sure stereo pairs match
        for (int i = 0; i < (std::min)(frames.size(), (size_t)CUBEMAP_MAX_FACES_COUNT); i++)
        {
            AVFrame*     leftFrame = frames[i];
            CubemapFace* leftFace  = cubemap->getEye(0)->getFace(i, true);
            
            AVFrame*     rightFrame = nullptr;
            CubemapFace* rightFace  = nullptr;
            
            if (frames.size() > i + CUBEMAP_MAX_FACES_COUNT)
            {
                rightFrame = frames[i+CUBEMAP_MAX_FACES_COUNT];
                rightFace  = cubemap->getEye(1)->getFace(i, true);
            }
            
            if (matchStereoPairs && frames.size() > i + CUBEMAP_MAX_FACES_COUNT)
            {
                // check if matched
                if (!leftFrame || !rightFrame)
                {
                    // if they don't match give them back and forget about them
                    sinks[i]->returnFrame(leftFrame);
                    sinks[i + CUBEMAP_MAX_FACES_COUNT]->returnFrame(rightFrame);
                    leftFrame  = nullptr;
                    rightFrame = nullptr;
                }
            }
            
            // Fill the cubemapfaces with pixels if pixels are available
            if (leftFrame)
            {
                count++;
                leftFace->setNewFaceFlag(true);
                memcpy(leftFace->getContent()->getPixels(), leftFrame->data[0], leftFrame->width * leftFrame->height * 4);
                sinks[i]->returnFrame(leftFrame);
            }
            else
            {
                leftFace->setNewFaceFlag(false);
            }
            
            if (rightFrame)
            {
                count++;
                rightFace->setNewFaceFlag(true);
                memcpy(rightFace->getContent()->getPixels(), rightFrame->data[0], rightFrame->width * rightFrame->height * 4);
                sinks[i + CUBEMAP_MAX_FACES_COUNT]->returnFrame(rightFrame);
            }
            else if (rightFace)
            {
                rightFace->setNewFaceFlag(false);
            }
        }

        //std::cout << count << std::endl;
        
        // Give it to the user of this library (AlloPlayer etc.)
        if (onNextCubemap)
        {
            if (lastPTS == 0)
            {
                lastPTS = pts;
            }
            
            if (lastDisplayTime.time_since_epoch().count() == 0)
            {
                lastDisplayTime = boost::chrono::system_clock::now();
                lastDisplayTime += boost::chrono::seconds(5);
            }
            
            // Wait until frame should be displayed
            if (lastPTS == 0)
            {
                lastPTS = pts;
            }
            
            uint64_t ptsDiff = pts - lastPTS;
            lastDisplayTime += boost::chrono::microseconds(ptsDiff);
            lastPTS = pts;
            
            boost::chrono::milliseconds sleepDuration = boost::chrono::duration_cast<boost::chrono::milliseconds>(lastDisplayTime - boost::chrono::system_clock::now());
            
//            if (sleepDuration.count() < 80)
//            {
//                lastDisplayTime += (boost::chrono::milliseconds(80) - sleepDuration);
//            }
//            
//            sleepDuration = boost::chrono::duration_cast<boost::chrono::milliseconds>(lastDisplayTime - boost::chrono::system_clock::now());
            
            //std::cout << "sleep duration " << sleepDuration.count() << "ms" << ", faces " << count << ", pending cubemaps " << pendingCubemaps << std::endl;
            //std::cout << "pts diff " << ptsDiff << std::endl;
            
            //boost::this_thread::sleep_until(lastDisplayTime);
            
            oldCubemap = onNextCubemap(this, cubemap);
		}
    }
}

H264CubemapSource::H264CubemapSource(std::vector<H264RawPixelsSink*>& sinks,
                                     AVPixelFormat                    format,
                                     bool                             matchStereoPairs)
    :
    sinks(sinks), format(format), oldCubemap(nullptr), lastDisplayPTS(0), matchStereoPairs(matchStereoPairs)
{
    
    av_log_set_level(AV_LOG_WARNING);
    int i = 0;
    for (H264RawPixelsSink* sink : sinks)
    {
        sink->setOnReceivedNALU       (boost::bind(&H264CubemapSource::sinkOnReceivedNALU,        this, _1, _2, _3));
        sink->setOnReceivedFrame      (boost::bind(&H264CubemapSource::sinkOnReceivedFrame,       this, _1, _2, _3));
        sink->setOnDecodedFrame       (boost::bind(&H264CubemapSource::sinkOnDecodedFrame,        this, _1, _2, _3));
        sink->setOnColorConvertedFrame(boost::bind(&H264CubemapSource::sinkOnColorConvertedFrame, this, _1, _2, _3));
        
        sinksFaceMap[sink] = i;
        i++;
    }
    getNextFramesThread  = boost::thread(boost::bind(&H264CubemapSource::getNextFramesLoop,  this));
    getNextCubemapThread = boost::thread(boost::bind(&H264CubemapSource::getNextCubemapLoop, this));
}

void H264CubemapSource::sinkOnReceivedNALU(H264RawPixelsSink* sink, u_int8_t type, size_t size)
{
    int face = sinksFaceMap[sink];
    if (onReceivedNALU) onReceivedNALU(this, type, size, face);
}

void H264CubemapSource::sinkOnReceivedFrame(H264RawPixelsSink* sink, u_int8_t type, size_t size)
{
    int face = sinksFaceMap[sink];
    if (onReceivedFrame) onReceivedFrame(this, type, size, face);
}

void H264CubemapSource::sinkOnDecodedFrame(H264RawPixelsSink* sink, u_int8_t type, size_t size)
{
    int face = sinksFaceMap[sink];
    if (onDecodedFrame) onDecodedFrame(this, type, size, face);
}

void H264CubemapSource::sinkOnColorConvertedFrame(H264RawPixelsSink* sink, u_int8_t type, size_t size)
{
    int face = sinksFaceMap[sink];
    if (onColorConvertedFrame) onColorConvertedFrame(this, type, size, face);
}

