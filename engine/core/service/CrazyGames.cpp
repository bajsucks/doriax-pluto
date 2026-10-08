// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "CrazyGames.h"

#include "Log.h"
#include "System.h"

using namespace doriax;

namespace {
    const char* notAvailableMessage = "CrazyGames is not available on this platform";

    void warnNotAvailable(){
        static bool warned = false;
        if (!warned){
            Log::warn("%s. Web exports have it.", notAvailableMessage);
            warned = true;
        }
    }

    CrazyGamesBackend* getBackend(){
        return System::instance().getCrazyGamesBackend();
    }
}

CrazyGamesEnvironment CrazyGames::environment = CrazyGamesEnvironment::UNINITIALIZED;

std::mutex CrazyGames::eventMutex;
std::vector<std::function<void()>> CrazyGames::pendingEvents;

FunctionSubscribe<void(CrazyGamesEnvironment)> CrazyGames::onInitialized;
FunctionSubscribe<void(CrazyGamesAdType)> CrazyGames::onAdStarted;
FunctionSubscribe<void(CrazyGamesAdType)> CrazyGames::onAdFinished;
FunctionSubscribe<void(CrazyGamesAdType, std::string, std::string)> CrazyGames::onAdError;

void CrazyGames::postEvent(std::function<void()> event){
    std::lock_guard<std::mutex> lock(eventMutex);
    pendingEvents.push_back(std::move(event));
}

void CrazyGames::dispatchEvents(){
    std::vector<std::function<void()>> events;
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        events.swap(pendingEvents);
    }
    // events posted by these callbacks wait for the next frame
    for (auto& event : events){
        event();
    }
}

void CrazyGames::clearSubscriptions(){
    onInitialized.clear();
    onAdStarted.clear();
    onAdFinished.clear();
    onAdError.clear();
}

void CrazyGames::removeSubscriptionsByTag(const std::string& substring){
    onInitialized.removeByTagSubstring(substring);
    onAdStarted.removeByTagSubstring(substring);
    onAdFinished.removeByTagSubstring(substring);
    onAdError.removeByTagSubstring(substring);
}

void CrazyGames::reset(){
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        pendingEvents.clear();
    }

    environment = CrazyGamesEnvironment::UNINITIALIZED;
}

void CrazyGames::initialize(){
    if (CrazyGamesBackend* backend = getBackend()){
        backend->initialize();
    }else{
        warnNotAvailable();
        postEvent([](){
            environment = CrazyGamesEnvironment::DISABLED;
            onInitialized.call(environment);
        });
    }
}

CrazyGamesEnvironment CrazyGames::getEnvironment(){
    return environment;
}

bool CrazyGames::isAvailable(){
    return environment == CrazyGamesEnvironment::LOCAL || environment == CrazyGamesEnvironment::CRAZYGAMES;
}

void CrazyGames::requestAd(CrazyGamesAdType type){
    if (CrazyGamesBackend* backend = getBackend()){
        backend->requestAd(type);
    }else{
        warnNotAvailable();
        postEvent([type](){ onAdError.call(type, "unavailable", notAvailableMessage); });
    }
}

void CrazyGames::gameplayStart(){
    if (CrazyGamesBackend* backend = getBackend()){
        backend->gameplayStart();
    }
}

void CrazyGames::gameplayStop(){
    if (CrazyGamesBackend* backend = getBackend()){
        backend->gameplayStop();
    }
}

void CrazyGames::loadingStart(){
    if (CrazyGamesBackend* backend = getBackend()){
        backend->loadingStart();
    }
}

void CrazyGames::loadingStop(){
    if (CrazyGamesBackend* backend = getBackend()){
        backend->loadingStop();
    }
}

void CrazyGames::happytime(){
    if (CrazyGamesBackend* backend = getBackend()){
        backend->happytime();
    }
}

void CrazyGames::systemInitialized(CrazyGamesEnvironment environment, const std::string& error){
    if (!error.empty()){
        Log::warn("CrazyGames SDK did not initialize: %s", error.c_str());
    }
    postEvent([environment](){
        CrazyGames::environment = environment;
        onInitialized.call(environment);
    });
}

void CrazyGames::systemAdStarted(CrazyGamesAdType type){
    postEvent([type](){ onAdStarted.call(type); });
}

void CrazyGames::systemAdFinished(CrazyGamesAdType type){
    postEvent([type](){ onAdFinished.call(type); });
}

void CrazyGames::systemAdError(CrazyGamesAdType type, const std::string& code, const std::string& message){
    postEvent([type, code, message](){ onAdError.call(type, code, message); });
}
