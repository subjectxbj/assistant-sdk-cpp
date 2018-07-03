#include <string>

class AssistantStateManager {
public:
    enum class State {
        IDLE,
        LISTENING,
        THINKING,
        SPEAKING,
        ERROR
    };
    void changeState(State state);
    void init(std::string ubus_sock);
private:
    void setLED(State state);
    void clearLED(State state);
    void updateLED(State state);
    void playSoundCue(State state);
    State m_state;
    std::string m_ubus_sock;
};
