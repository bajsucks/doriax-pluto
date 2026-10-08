// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef CRAZYGAMES_H
#define CRAZYGAMES_H

#include "Export.h"
#include "util/FunctionSubscribe.h"
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace doriax {

    // Sent to the platform as integers, so keep the order
    enum class CrazyGamesAdType{
        MIDGAME,
        REWARDED
    };

    enum class CrazyGamesEnvironment{
        UNINITIALIZED,
        DISABLED, // another domain, or the SDK is missing or failed to load
        LOCAL, // localhost: ads are replaced by an overlay text
        CRAZYGAMES
    };

    // What a platform with the CrazyGames SDK implements, see System::getCrazyGamesBackend.
    // Called from the engine thread, it answers through the CrazyGames::system* callbacks.
    class DORIAX_API CrazyGamesBackend {
    public:
        virtual ~CrazyGamesBackend() = default;

        // Calls made before the SDK finishes initializing wait for it
        virtual void initialize() = 0;
        virtual void requestAd(CrazyGamesAdType type) = 0;
        virtual void gameplayStart() = 0;
        virtual void gameplayStop() = 0;
        virtual void loadingStart() = 0;
        virtual void loadingStop() = 0;
        virtual void happytime() = 0;
    };

    // CrazyGames HTML5 SDK on Web. Results arrive as events at the start of a frame,
    // and the engine pauses while an ad plays.
    class DORIAX_API CrazyGames {

        friend class Engine;

    private:
        static CrazyGamesEnvironment environment;

        static std::mutex eventMutex;
        static std::vector<std::function<void()>> pendingEvents;

        static void postEvent(std::function<void()> event);

        // Called by Engine
        static void dispatchEvents();
        static void clearSubscriptions();
        static void removeSubscriptionsByTag(const std::string& substring);
        static void reset();

    public:
        // Loads the SDK; onInitialized tells the environment it runs in
        static void initialize();
        static CrazyGamesEnvironment getEnvironment();
        // Initialized on CrazyGames or localhost
        static bool isAvailable();

        // A rewarded ad grants its reward with onAdFinished
        static void requestAd(CrazyGamesAdType type);

        // Gameplay starts at play and resume, and stops at every break: menus, pauses, level ends
        static void gameplayStart();
        static void gameplayStop();
        static void loadingStart();
        static void loadingStop();
        // A happy moment, like a level completed
        static void happytime();

        static FunctionSubscribe<void(CrazyGamesEnvironment)> onInitialized;
        static FunctionSubscribe<void(CrazyGamesAdType)> onAdStarted;
        static FunctionSubscribe<void(CrazyGamesAdType)> onAdFinished;
        // code from the SDK, like unfilled or adCooldown, or unavailable from the engine
        static FunctionSubscribe<void(CrazyGamesAdType, std::string, std::string)> onAdError;

        // Platform callbacks, safe from any thread
        static void systemInitialized(CrazyGamesEnvironment environment, const std::string& error);
        static void systemAdStarted(CrazyGamesAdType type);
        static void systemAdFinished(CrazyGamesAdType type);
        static void systemAdError(CrazyGamesAdType type, const std::string& code, const std::string& message);
    };

}

#endif //CRAZYGAMES_H
