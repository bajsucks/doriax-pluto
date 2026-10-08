// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef WEBPORTAL_H
#define WEBPORTAL_H

#include "Export.h"
#include "util/FunctionSubscribe.h"
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace doriax {

    // The game portal a web export targets, chosen in the project settings
    enum class WebPortalType{
        NONE,
        CRAZYGAMES,
        POKI
    };

    // Sent to the platform as integers, so keep the order
    enum class WebPortalAdType{
        MIDGAME,
        REWARDED
    };

    enum class WebPortalEnvironment{
        UNINITIALIZED,
        DISABLED, // no portal SDK, it failed to load, or the portal refuses this domain
        LOCAL, // localhost, where the portal shows test ads
        PORTAL
    };

    // What a platform with a portal SDK implements, see System::getWebPortalBackend.
    // Called from the engine thread, it answers through the WebPortal::system* callbacks.
    class DORIAX_API WebPortalBackend {
    public:
        virtual ~WebPortalBackend() = default;

        virtual WebPortalType getType() = 0;
        // Calls made before the SDK finishes initializing wait for it
        virtual void initialize() = 0;
        virtual void requestAd(WebPortalAdType type) = 0;
        virtual void gameplayStart() = 0;
        virtual void gameplayStop() = 0;
        virtual void loadingStart() = 0;
        virtual void loadingStop() = 0;
        virtual void happytime() = 0;
    };

    // Game portal SDKs of web exports, like CrazyGames and Poki. Results arrive as events
    // at the start of a frame, and the engine pauses while an ad plays.
    class DORIAX_API WebPortal {

        friend class Engine;

    private:
        static WebPortalEnvironment environment;

        static std::mutex eventMutex;
        static std::vector<std::function<void()>> pendingEvents;

        static void postEvent(std::function<void()> event);

        // Called by Engine
        static void dispatchEvents();
        static void clearSubscriptions();
        static void removeSubscriptionsByTag(const std::string& substring);
        static void reset();

    public:
        static WebPortalType getPortal();

        // Loads the portal SDK; onInitialized tells the environment it runs in
        static void initialize();
        static WebPortalEnvironment getEnvironment();
        // Initialized on the portal or localhost
        static bool isAvailable();

        // A rewarded ad grants its reward with onAdFinished. Portals decide when a midgame
        // ad plays, so a request may show none.
        static void requestAd(WebPortalAdType type);

        // Gameplay starts at play and resume, and stops at every break: menus, pauses, level ends
        static void gameplayStart();
        static void gameplayStop();
        static void loadingStart();
        static void loadingStop();
        // A happy moment, like a level completed
        static void happytime();

        static FunctionSubscribe<void(WebPortalEnvironment)> onInitialized;
        static FunctionSubscribe<void(WebPortalAdType)> onAdStarted;
        static FunctionSubscribe<void(WebPortalAdType)> onAdFinished;
        // code: unfilled when no ad played, unavailable without a portal, or the portal's own
        static FunctionSubscribe<void(WebPortalAdType, std::string, std::string)> onAdError;

        // Platform callbacks, safe from any thread
        static void systemInitialized(WebPortalEnvironment environment, const std::string& error);
        static void systemAdStarted(WebPortalAdType type);
        static void systemAdFinished(WebPortalAdType type);
        static void systemAdError(WebPortalAdType type, const std::string& code, const std::string& message);
    };

}

#endif //WEBPORTAL_H
