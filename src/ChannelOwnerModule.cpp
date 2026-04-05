#include "ChannelOwnerModule.h"

WCLChannelOwnerModule::WCLChannelOwnerModule(uint8_t numberOfChannels)
    : _numberOfChannels(numberOfChannels)
{
    if (_numberOfChannels > 0)
    {
        _pChannels = new OpenKNX::Channel*[numberOfChannels]();
    }
}

WCLChannelOwnerModule::~WCLChannelOwnerModule()
{
    if (_pChannels != nullptr)
    {
        delete[] _pChannels;
        _pChannels = nullptr;
    }
}

void WCLChannelOwnerModule::setup(bool configured)
{
    OpenKNX::Module::setup(configured);
}

OpenKNX::Channel* WCLChannelOwnerModule::createChannel(uint8_t _channelIndex /* this parameter is used in macros, do not rename */)
{
    return nullptr;
}

void WCLChannelOwnerModule::setup()
{
    OpenKNX::Module::setup();
    if (_pChannels != nullptr)
    {
        logDebugP("Setting up %d channels", _numberOfChannels);
        for (uint8_t _channelIndex = 0; _channelIndex < _numberOfChannels; _channelIndex++)
        {
            logDebugP("Create channel %d", _channelIndex);
            logIndentUp();
            _pChannels[_channelIndex] = createChannel(_channelIndex);
            logIndentDown();
        }
        for (uint8_t _channelIndex = 0; _channelIndex < _numberOfChannels; _channelIndex++)
        {
            OpenKNX::Channel* channel = _pChannels[_channelIndex];
            if (channel != nullptr)
            {
                logDebugP("Init channel %d", _channelIndex);
                logIndentUp();
                channel->init();
                logIndentDown();

                logDebugP("Setup channel %d - setup(true)", _channelIndex);
                logIndentUp();
                channel->setup(true);
                logIndentDown();

                logInfoP("Setup channel %d - setup()", _channelIndex);
                logIndentUp();
                channel->setup();
                logIndentDown();
            }
        }
    }
}

void WCLChannelOwnerModule::loop(bool configured)
{
    OpenKNX::Module::loop(configured);
    if (_pChannels != nullptr)
    {
        uint8_t processed = 0;
        do
        {
            OpenKNX::Channel* channel = _pChannels[_currentChannel];
            if (channel != nullptr)
            {
                channel->loop(configured);
            }
        }
        while (openknx.freeLoopIterate(_numberOfChannels, _currentChannel, processed));
    }
}

void WCLChannelOwnerModule::loop()
{
    OpenKNX::Module::loop();
    if (_pChannels != nullptr)
    {
        for (uint8_t _channelIndex = 0; _channelIndex < _numberOfChannels; _channelIndex++)
        {
            OpenKNX::Channel* channel = _pChannels[_channelIndex];
            if (channel != nullptr)
                channel->loop();
        }
    }
}

OpenKNX::Channel* WCLChannelOwnerModule::getChannel(uint8_t channelIndex)
{
    return _pChannels != nullptr ? _pChannels[channelIndex] : nullptr;
}

uint8_t WCLChannelOwnerModule::getNumberOfChannels()
{
    return _numberOfChannels;
}

uint8_t WCLChannelOwnerModule::getNumberOfUsedChannels()
{
    uint8_t activeChannels = 0;
    if (_pChannels != nullptr)
    {
        for (uint8_t _channelIndex = 0; _channelIndex < _numberOfChannels; _channelIndex++)
        {
            OpenKNX::Channel* channel = _pChannels[_channelIndex];
            if (channel != nullptr)
                activeChannels++;
        }
    }
    return activeChannels;
}

#ifdef OPENKNX_DUALCORE
void WCLChannelOwnerModule::setup1(bool configured)
{
    OpenKNX::Module::setup1(configured);
    if (_pChannels != nullptr)
    {
        for (uint8_t _channelIndex = 0; _channelIndex < _numberOfChannels; _channelIndex++)
        {
            OpenKNX::Channel* channel = _pChannels[_channelIndex];
            if (channel != nullptr)
                channel->setup1(configured);
        }
    }
}

void WCLChannelOwnerModule::setup1()
{
    OpenKNX::Module::setup1();
    if (_pChannels != nullptr)
    {
        for (uint8_t _channelIndex = 0; _channelIndex < _numberOfChannels; _channelIndex++)
        {
            OpenKNX::Channel* channel = _pChannels[_channelIndex];
            if (channel != nullptr)
                channel->setup1();
        }
    }
}

void WCLChannelOwnerModule::loop1(bool configured)
{
    OpenKNX::Module::loop1(configured);
    if (_pChannels != nullptr)
    {
        for (uint8_t _channelIndex = 0; _channelIndex < _numberOfChannels; _channelIndex++)
        {
            OpenKNX::Channel* channel = _pChannels[_channelIndex];
            if (channel != nullptr)
                channel->loop1(configured);
        }
    }
}

void WCLChannelOwnerModule::loop1()
{
    OpenKNX::Module::loop1();
    if (_pChannels != nullptr)
    {
        for (uint8_t _channelIndex = 0; _channelIndex < _numberOfChannels; _channelIndex++)
        {
            OpenKNX::Channel* channel = _pChannels[_channelIndex];
            if (channel != nullptr)
                channel->loop1();
        }
    }
}
#endif
