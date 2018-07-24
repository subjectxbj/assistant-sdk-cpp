#include "state_manager.h"
#include <string>
#include <map>
#include <iostream>
#include <thread>

extern "C" {
#include <unistd.h>

#include <libubox/blobmsg_json.h>
#include <libubus.h>
}


static const std::map<AssistantStateManager::State, std::string> assistant_states = {
	{AssistantStateManager::State::IDLE,                 "c_alexa_idle"}, 
	{AssistantStateManager::State::LISTENING,            "c_alexa_listening"}, 
	{AssistantStateManager::State::THINKING,             "c_alexa_thinking"}, 
	{AssistantStateManager::State::SPEAKING,             "c_alexa_responding"}, 
	{AssistantStateManager::State::ERROR,                "c_alexa_system_error"} 
    };

static struct blob_buf b;

static const int timeout = 30;

static void receive_call_result_data(struct ubus_request *req, int type, struct blob_attr *msg)
{
        char *str;
        if (!msg)
                return;

        str = blobmsg_format_json_indent(msg, true, -1);
        std::cout << str << std::endl;
        free(str);
}

static int ubus_call(struct ubus_context *ctx, const char *path, const char *method, const char *message){
    uint32_t id;
    int ret;

    blob_buf_init(&b, 0);
    if (message && strlen(message)) {
        ret = blobmsg_add_json_from_string(&b, message);
        if (!ret) {
            std::cerr << "failed to parse message data" << std::endl;
            return -1;
        }
    }

    ret = ubus_lookup_id(ctx, path, &id);

    if (ret) {
        std::cerr << "failed to lookup id" << std::endl;
        return ret;
    }
    return ubus_invoke(ctx, id, method, b.head, receive_call_result_data, NULL, timeout*10000);

}

void AssistantStateManager::changeState(AssistantStateManager::State state){
    m_state = state;
    updateLED(state);
    
    act_thread.reset(new std::thread([this, state]() {
        if (state == AssistantStateManager::State::LISTENING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        playSoundCue(state);
    }));
    
    act_thread->join();
    act_thread.reset(nullptr);
}
void AssistantStateManager::init(std::string ubus_sock){
    m_ubus_sock = ubus_sock;
    clearLED(AssistantStateManager::State::LISTENING);
    clearLED(AssistantStateManager::State::THINKING);
    clearLED(AssistantStateManager::State::SPEAKING);
}
void AssistantStateManager::setLED(AssistantStateManager::State state){
    int ret;
    static struct ubus_context *ctx;
    char msg[1024];
    std::string state_string = assistant_states.at(state);

    ctx = ubus_connect(m_ubus_sock.c_str());
    if (!ctx){
        std::cerr << "Failed to connect to ubus" <<std::endl;
        return;
    }

    sprintf(msg, "{\"name\":\"%s\"}", state_string.c_str());
    ret = ubus_call(ctx, "ledmgr", "set_condition", msg);
    std::cout << "ubus_call ret " << ret << std::endl;
    ubus_free(ctx);    
        
}
void AssistantStateManager::clearLED(AssistantStateManager::State state){
    int ret;
    static struct ubus_context *ctx;
    char msg[1024];
    std::string state_string = assistant_states.at(state);

    ctx = ubus_connect(m_ubus_sock.c_str());
    if (!ctx){
        std::cerr << "Failed to connect to ubus" << std::endl;
        return;
    }

    sprintf(msg, "{\"name\":\"%s\"}", state_string.c_str());
    ret = ubus_call(ctx, "ledmgr", "clear_condition", msg);
    std::cout << "ubus_call ret " << ret << std::endl;
    ubus_free(ctx);
}
void AssistantStateManager::updateLED(AssistantStateManager::State new_state) {
    static AssistantStateManager::State old_state = AssistantStateManager::State::IDLE;

    if (old_state == new_state) {
        return;
    }
    if (old_state == AssistantStateManager::State::IDLE) {
        if (new_state != AssistantStateManager::State::IDLE) {
            setLED(new_state);
            old_state = new_state;
        }
    }else {
        if (new_state != AssistantStateManager::State::IDLE) {
            clearLED(old_state);
            setLED(new_state);
            old_state = new_state;
        }else{
            clearLED(old_state);
            old_state = AssistantStateManager::State::IDLE;
        }
    }
}

void AssistantStateManager::playSoundCue(AssistantStateManager::State state){ 
    char command[1024];

    if (state == AssistantStateManager::State::LISTENING) {
        sprintf(command, "aplay /etc/sounds/ful_ui_wakesound.wav");
        system(command);
    
    } else if (state == AssistantStateManager::State::THINKING) {
        sprintf(command, "aplay /etc/sounds/ful_ui_endpointing.wav");
        system(command);
    }
}
