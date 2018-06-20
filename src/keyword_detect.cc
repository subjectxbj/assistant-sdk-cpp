#include "keyword_detect.h"
#include <alsa/asoundlib.h>
#include "snsr.h"

using namespace std;


string SNSR_MODEL_FILE ("/spot-alexa-rpi-31000.snsr");


static std::string getSensoryDetails(SnsrSession session, SnsrRC result) {
    std::string message;
    // It is recommended by Sensory to prefer snsrErrorDetail() over snsrRCMessage() as it provides more details.
    if (session) {
        message = snsrErrorDetail(session);
    } else {
        message = snsrRCMessage(result);
    }
    if (message.empty()) {
        message = "Unrecognized error";
    }
    return message;
}

void KeywordDetect::InitSNSR() {

    SnsrRC result = snsrNew(&m_session);
    if (result != SNSR_RC_OK) {
        printf("snsrNew error %s\n", getSensoryDetails(m_session, result).c_str());
        return;
    } 
    
    result = snsrLoad(m_session, snsrStreamFromFileName(SNSR_MODEL_FILE.c_str(), "r"));
    if (result != SNSR_RC_OK) {
        printf("snsrLoad error %s\n", getSensoryDetails(m_session, result).c_str());
        return;
    }
    
    result = snsrRequire(m_session, SNSR_TASK_TYPE, SNSR_PHRASESPOT);
    if (result != SNSR_RC_OK) {
        printf("snsrRequire error %s\n", getSensoryDetails(m_session, result).c_str());
        return;
    }
    
    if (!setUpRuntimeSettings(&m_session)) {
        return ;
    }
    printf("KeywordDetect::InitSNSR\n");    
}

bool KeywordDetect::InitPCM() {
    int pcm_open_ret = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (pcm_open_ret < 0) {
        std::cerr << "KeywordDetect snd_pcm_open returned " << pcm_open_ret << std::endl;
        return false;
    }

    snd_pcm_hw_params_t* pcm_params;
    int malloc_param_ret = snd_pcm_hw_params_malloc(&pcm_params);
    if (malloc_param_ret < 0) {
        std::cerr << "KeywordDetect snd_pcm_hw_params_malloc returned " << malloc_param_ret
            << std::endl;
        return false;
    }

    snd_pcm_hw_params_any(pcm_handle, pcm_params);
    int set_param_ret = snd_pcm_hw_params_set_access(pcm_handle, pcm_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (set_param_ret < 0) {
        std::cerr << "KeywordDetect snd_pcm_hw_params_set_access returned " << set_param_ret
            << std::endl;
        return false;
    }

    set_param_ret = snd_pcm_hw_params_set_format(pcm_handle, pcm_params, SND_PCM_FORMAT_S16_LE);
    if (set_param_ret < 0) {
        std::cerr << "KeywordDetect snd_pcm_hw_params_set_format returned " << set_param_ret
            << std::endl;
        return false;
    }
    set_param_ret = snd_pcm_hw_params_set_channels(pcm_handle, pcm_params, 1);
    if (set_param_ret < 0) {
        std::cerr << "AudioOutputALSA snd_pcm_hw_params_set_channels returned " << set_param_ret
            << std::endl;
        return false;
    }
    unsigned int rate = 16000;
    set_param_ret = snd_pcm_hw_params_set_rate_near(pcm_handle, pcm_params, &rate, nullptr);
    if (set_param_ret < 0) {
        std::cerr << "AudioOutputALSA snd_pcm_hw_params_set_rate_near returned " << set_param_ret
            << std::endl;
        return false;
    }
    set_param_ret = snd_pcm_hw_params(pcm_handle, pcm_params);
    if (set_param_ret < 0) {
        std::cerr << "AudioOutputALSA snd_pcm_hw_params returned " << set_param_ret << std::endl;
        return false;
    }
    snd_pcm_hw_params_free(pcm_params);
    printf("KeywordDetect::InitPCM\n");    
}

void KeywordDetect::Start() {
    m_isRunning = true;
    printf("KeywordDetect::Start\n");    
}

void KeywordDetect::Stop() {
    //m_isRunning = false;
    if (loopThread->joinable()) {
        loopThread->join();
    }
}

void KeywordDetect::AnalyzeAudio(std::shared_ptr<std::vector<unsigned char>> data){
    SnsrRC result;
    bool didErrorOccur = false;
    snsrSetStream(
        m_session,
        SNSR_SOURCE_AUDIO_PCM,
        snsrStreamFromMemory(
                    &((*data)[0]), data->size(), SNSR_ST_MODE_READ)
    );
    result = snsrRun(m_session);
    switch (result) {
        case SNSR_RC_STREAM_END:
                    // Reached end of buffer without any keyword detections
                    break;
        case SNSR_RC_OK:
                    break;
        default:
                    // A different return from the callback function that indicates some sort of error
            std::cerr << "KeywordDetect::AnalyzeAudio()" << getSensoryDetails(m_session, result);

            didErrorOccur = true;
            break;
    }
    if (didErrorOccur) {
       std::cerr<< "KeywordDetect::AnalyzeAudio()" << "ERROR" <<std::endl;
    }
}
bool KeywordDetect::setUpRuntimeSettings(SnsrSession* session) {
    if (!session) {
        std::cerr << "KeywordDetect::setUpRuntimeSettings() session is null" << std::endl;
        return false;
    }

    // Setting the callback handler
    SnsrRC result = snsrSetHandler(
        *session, SNSR_RESULT_EVENT, snsrCallback(keyWordDetectedCallback, nullptr, reinterpret_cast<void*>(this)));

    if (result != SNSR_RC_OK) {
        std::cerr << "KeywordDetect::setUpRuntimeSettings()" << getSensoryDetails(*session, result);
        return false;
    }

    /*
     * Turns off automatic pipeline flushing that happens when the end of the input stream is reached. This is an
     * internal setting recommended by Sensory when audio is presented to Sensory in small chunks.
     */
    result = snsrSetInt(*session, SNSR_AUTO_FLUSH, 0);
    if (result != SNSR_RC_OK) {
        std::cerr << "KeywordDetect::setUpRuntimeSettings()" << getSensoryDetails(*session, result);
        return false;
    }

    return true;
}

SnsrRC KeywordDetect::keyWordDetectedCallback(SnsrSession s, const char* key, void* userData) {
    SnsrRC result;
    const char* keyword;
    double begin;
    double end;
    result = snsrGetDouble(s, SNSR_RES_BEGIN_SAMPLE, &begin);
    if (result != SNSR_RC_OK) {
        std::cerr << "KeywordDetect::keyWordDetectedCallback()" << getSensoryDetails(s, result);
        return result;
    }

    result = snsrGetDouble(s, SNSR_RES_END_SAMPLE, &end);
    if (result != SNSR_RC_OK) {
        std::cerr << "KeywordDetect::keyWordDetectedCallback()" << getSensoryDetails(s, result);
        return result;
    }

    result = snsrGetString(s, SNSR_RES_TEXT, &keyword);
    if (result != SNSR_RC_OK) {
        std::cerr << "KeywordDetect::keyWordDetectedCallback()" << getSensoryDetails(s, result);
        return result;
    }
    std:cout << "KeywordDetect::keyWordDetectedCallback()" << " keyword: " << keyword << std::endl; 

    if (strcmp(keyword, "alexa") == 0)
    {
        KeywordDetect *p = (KeywordDetect*)userData;
        p->m_isRunning = false;
    }

    return SNSR_RC_OK;
}

void KeywordDetect::Loop() {
    printf("KeywordDetect::Loop\n");
    loopThread = std::unique_ptr<std::thread>(new std::thread([this]() {

    printf("KeywordDetect::Thread\n");
    SnsrRC result;

    InitPCM();

    while (m_isRunning) {
      std::shared_ptr<std::vector<unsigned char>> audio_data(
            new std::vector<unsigned char>(kFramesPerPacket * kBytesPerFrame));

      int pcm_read_ret = snd_pcm_readi(pcm_handle, &(*audio_data.get())[0], kFramesPerPacket);
      if (pcm_read_ret == -EAGAIN) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
      } else if (pcm_read_ret == -EBADFD) {
          std::cerr << "KeywordDetect::Loop -EBADFD" <<std::endl;
      } else if (pcm_read_ret == -EPIPE) {
          std::cerr << "KeywordDetect::Loop -EPIPE" <<std::endl;
          SnsrSession newSession{nullptr};
            /*
             * This duplicated SnsrSession will have all the same configurations as m_session but none of the runtime
             * settings. Thus, we will need to setup some of the runtime settings again. The reason for creating a new
             * session is so that on overrun conditions, Sensory can start counting from 0 again.
             */
          result = snsrDup(m_session, &newSession);
          if (result != SNSR_RC_OK) {
              break;
          }

          if (!setUpRuntimeSettings(&newSession)) {
              break;
          }

          m_session = newSession;
      } else if (pcm_read_ret == -ESTRPIPE) {
          std::cerr << "KeywordDetect::Loop -ESTRPIPE" <<std::endl;
      } else if (pcm_read_ret > 0) {
          std::cout << "KeywordDetect::Loop read audio data " << pcm_read_ret << "bytes" << std::endl;
          audio_data->resize(kBytesPerFrame * pcm_read_ret);
          AnalyzeAudio(audio_data);
          snsrClearRC(m_session);
/*
          for (auto& listener : data_listeners_) {
              listener(audio_data);
          }
*/
      }
    }

    // Finalize.
    snd_pcm_close(pcm_handle);
    std::cout << "KeywordDetect::Loop Exit" << std::endl;

  }));     
}
