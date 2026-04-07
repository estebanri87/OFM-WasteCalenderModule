#pragma once
#include "OpenKNX.h"

class WCLChannelOwnerModule : public OpenKNX::Module
{
  private:
    uint8_t _numberOfChannels;
    uint8_t _currentChannel = 0;
    OpenKNX::Channel** _pChannels = nullptr;

  public:
    WCLChannelOwnerModule(uint8_t numberOfChannels = 0);
    ~WCLChannelOwnerModule();

    virtual OpenKNX::Channel* createChannel(uint8_t _channelIndex /* this parameter is used in macros, do not rename */);

    virtual void setup(bool configured) override;
    virtual void setup() override;

    virtual void loop(bool configured) override;
    virtual void loop() override;

    uint8_t getNumberOfUsedChannels();
    uint8_t getNumberOfChannels();
    OpenKNX::Channel* getChannel(uint8_t channelIndex);

    virtual void processInputKo(GroupObject& ko) override;

#ifdef OPENKNX_DUALCORE
    virtual void setup1(bool configured) override;
    virtual void setup1() override;
    virtual void loop1(bool configured) override;
    virtual void loop1() override;
#endif
};
