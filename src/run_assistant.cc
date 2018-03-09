/*
Copyright 2017 Google Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <grpc++/grpc++.h>

#include <getopt.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <iostream>
#include <fstream>
#include <sstream>

#ifdef __linux__
#define ENABLE_ALSA
#endif

#ifdef ENABLE_ALSA
#include "audio_input_alsa.h"
#include "audio_output_alsa.h"
#endif

#include "embedded_assistant.pb.h"
#include "embedded_assistant.grpc.pb.h"

#include "assistant_config.h"
#include "audio_input.h"
#include "audio_input_file.h"
#include "json_util.h"

using google::assistant::embedded::v1alpha1::EmbeddedAssistant;
using google::assistant::embedded::v1alpha1::ConverseRequest;
using google::assistant::embedded::v1alpha1::ConverseResponse;
using google::assistant::embedded::v1alpha1::AudioInConfig;
using google::assistant::embedded::v1alpha1::AudioOutConfig;
using google::assistant::embedded::v1alpha1::ConverseResponse_EventType_END_OF_UTTERANCE;
using google::assistant::embedded::v1alpha1::ConverseResult_MicrophoneMode_CLOSE_MICROPHONE;
using google::assistant::embedded::v1alpha1::ConverseResult_MicrophoneMode_DIALOG_FOLLOW_ON;

using grpc::CallCredentials;
using grpc::Channel;
using grpc::ClientReaderWriter;

static const std::string kCredentialsTypeUserAccount = "USER_ACCOUNT";
static const std::string kALSAAudioInput = "ALSA_INPUT";

// Creates a channel to be connected to Google.
std::shared_ptr<Channel> CreateChannel(const std::string& host) {
  std::ifstream file("robots.pem");
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string roots_pem = buffer.str();

  std::cout << "assistant_sdk robots_pem: " << roots_pem << std::endl;
  ::grpc::SslCredentialsOptions ssl_opts = {roots_pem, "", ""};
  auto creds = ::grpc::SslCredentials(ssl_opts);
  std::string server = host + ":443";
  std::cout << "assistant_sdk CreateCustomChannel(" << server << ", creds, arg)"

      << std::endl << std::endl;
  ::grpc::ChannelArguments channel_args;
  return CreateCustomChannel(server, creds, channel_args);
}

void PrintUsage() {
  std::cerr << "Usage: ./run_assistant "
            << "--audio_input <" << kALSAAudioInput << "|<audio_file>] "
            << "--credentials_file <credentials_file> "
            << "--credentials_type <" << kCredentialsTypeUserAccount << "> "
            << "[--api_endpoint <API endpoint>]" << std::endl;
}

bool GetCommandLineFlags(
    int argc, char** argv, std::string* audio_input,
    std::string* credentials_file_path, std::string* credentials_type,
    std::string* api_endpoint) {
  const struct option long_options[] = {
    {"audio_input",      required_argument, nullptr, 'i'},
    {"credentials_file", required_argument, nullptr, 'f'},
    {"credentials_type", required_argument, nullptr, 't'},
    {"api_endpoint",     required_argument, nullptr, 'e'},
    {nullptr, 0, nullptr, 0}
  };
  *api_endpoint = ASSISTANT_ENDPOINT;
  while (true) {
    int option_index;
    int option_char =
        getopt_long(argc, argv, "i:f:t:e", long_options, &option_index);
    if (option_char == -1) {
      break;
    }
    switch (option_char) {
      case 'i':
        *audio_input = optarg;
        break;
      case 'f':
        *credentials_file_path = optarg;
        break;
      case 't':
        *credentials_type = optarg;
        if (*credentials_type != kCredentialsTypeUserAccount) {
          std::cerr << "Invalid credentials_type: \"" << *credentials_type
                    << "\". Should be \"" << kCredentialsTypeUserAccount
                    << "\"" << std::endl;
          return false;
        }
        break;
      case 'e':
        *api_endpoint = optarg;
        break;
      default:
        PrintUsage();
        return false;
    }
  }
  return true;
}

ConverseRequest MakeConverseRequestConfig(){
  ConverseRequest req;
  auto* converse_config = req.mutable_config();
  converse_config->mutable_audio_in_config()->set_encoding(AudioInConfig::LINEAR16);
  converse_config->mutable_audio_in_config()->set_sample_rate_hertz(16000);
  converse_config->mutable_audio_out_config()->set_encoding(AudioOutConfig::LINEAR16);
  converse_config->mutable_audio_out_config()->set_sample_rate_hertz(16000);
  auto* converse_context = converse_config->mutable_converse_state()->mutable_context();
  converse_context->set_third_party_context("{'current_channel': 'News'}");
  return req;
}

int StartDialog(std::shared_ptr<EmbeddedAssistant::Stub> assistant,
				std::shared_ptr<CallCredentials> call_credentials,
				std::shared_ptr<AudioOutputALSA> audio_output) {
	// ConverseRequest Audio in
	ConverseRequest request_audio_in;
	// ConverseResponse
	ConverseResponse response;
	// AudioInput
	std::unique_ptr<AudioInput> audio_input;
	// AudioOutput
	// AudioOutputALSA audio_output;
	// Start Audio Output Thread
	audio_output->Start();
	std::cout << std::endl << "*****PLEASE SPEAK YOUR REQUEST.";
	// Begin a stream.
	grpc::ClientContext context;
	context.set_fail_fast(false);
	context.set_credentials(call_credentials);

	std::shared_ptr<ClientReaderWriter<ConverseRequest, ConverseResponse>>
	  stream(std::move(assistant->Converse(&context)));
	  
	// Reset Audio Input
	audio_input.reset(new AudioInputALSA());
	audio_input->AddDataListener(
		[stream, &request_audio_in](std::shared_ptr<std::vector<unsigned char>> data) {
			request_audio_in.set_audio_in(&((*data)[0]), data->size());
			stream->Write(request_audio_in);
		}
	);
	audio_input->AddStopListener([stream]() {
		stream->WritesDone();
	});
	// Send ConverseRequest Config to Google
	stream->Write(MakeConverseRequestConfig());
	// Start Audio Input Thread
	audio_input->Start();
  
	// Start reading response
	while (stream->Read(&response)) {  // Returns false when no more to read.
		//std::cout << "assistant_sdk Got a response \n";
		if (audio_input->IsRunning()) {
			if (response.has_error()) {
				std::cout << std::endl << "*****RESPONSE ERROR.";
				audio_input->Stop();
			}
			if(response.has_audio_out()) {
				std::cout << std::endl << "*****RESPONSE RECEIVED.";
				audio_input->Stop();
			}
			if(response.event_type() == ConverseResponse_EventType_END_OF_UTTERANCE) {
				std::cout << std::endl << "*****END OF UTTERANCE.";
				audio_input->Stop();
			}
		}
    
		if (response.result().microphone_mode() == ConverseResult_MicrophoneMode_CLOSE_MICROPHONE) {
			std::cout << std::endl << "*****CLOSE MICROPHONE.";
		}
		else if (response.result().microphone_mode() == ConverseResult_MicrophoneMode_DIALOG_FOLLOW_ON) {
			std::cout << std::endl << "*****DIALOG FOLLOW ON.";
		}
	
		// Playback the response audio
		if (response.has_audio_out()) {
			std::shared_ptr<std::vector<unsigned char>>
				data(new std::vector<unsigned char>);
			data->resize(response.audio_out().audio_data().length());
			memcpy(&((*data)[0]), response.audio_out().audio_data().c_str(), response.audio_out().audio_data().length());
			audio_output->Send(data);
		}
		// Display the text of the request
		if (response.has_interim_spoken_request_text()) {
			// CUSTOMIZE: render interim spoken request on screen
			std::cout << "assistant_sdk response: \n"
                << response.ShortDebugString() << std::endl;
		}
	}

	
	// Destroy the stream
	grpc::Status status = stream->Finish();
	if (!status.ok()) {
		// Report the RPC failure.
		std::cerr << "assistant_sdk failed, error: " <<
              status.error_message() << std::endl;
		return -1;
	}
	// Stop the Audio Output Thread
	audio_output->Stop();
}

int main(int argc, char** argv) {
	std::string audio_input_source, credentials_file_path, credentials_type,api_endpoint;
	if (!GetCommandLineFlags(argc, argv, &audio_input_source,
						   &credentials_file_path, &credentials_type,
						   &api_endpoint)) {
		return -1;
	}


	// Read credentials file.
	std::ifstream credentials_file(credentials_file_path);
	if (!credentials_file) {
		std::cerr << "Credentials file \"" << credentials_file_path
			  << "\" does not exist." << std::endl;
		return -1;
	}
	std::stringstream credentials_buffer;
	credentials_buffer << credentials_file.rdbuf();
	std::string credentials = credentials_buffer.str();
	std::shared_ptr<CallCredentials> call_credentials;
	call_credentials = grpc::GoogleRefreshTokenCredentials(credentials);
	if (call_credentials.get() == nullptr) {
		std::cerr << "Credentials file \"" << credentials_file_path
			  << "\" is invalid. Check step 5 in README for how to get valid "
			  << "credentials." << std::endl;
		return -1;
	}
	// Setup Channel to IW with Google
	auto channel = CreateChannel(api_endpoint);
	std::shared_ptr<EmbeddedAssistant::Stub> assistant(EmbeddedAssistant::NewStub(channel));
	
	std::shared_ptr<AudioOutputALSA> audio_output(new AudioOutputALSA());

	while(1) {
		StartDialog(assistant, call_credentials, audio_output);
	}
	return 0;
}
