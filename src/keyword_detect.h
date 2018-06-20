#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <iostream>
#include <atomic>
#include "snsr.h"
#include <alsa/asoundlib.h>

class KeywordDetect {

public:
   void InitSNSR();
   bool InitPCM();
   void Start();
   void Stop();
   void Loop();
   void AnalyzeAudio(std::shared_ptr<std::vector<unsigned char>> data);
   bool setUpRuntimeSettings(SnsrSession* session);
   static SnsrRC keyWordDetectedCallback(SnsrSession s, const char* key, void* userData);
private:
   std::unique_ptr<std::thread> loopThread;
   std::vector<int16_t> audio_data;
   SnsrSession m_session;
   std::atomic<bool> m_isRunning;
   snd_pcm_t *pcm_handle;
// For 16000Hz, it's about 0.1 second.
  static constexpr int kFramesPerPacket = 8000;
  // 1 channel, S16LE, so 2 bytes each frame.
  static constexpr int kBytesPerFrame = 2;
};
