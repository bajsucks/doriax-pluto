// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SYSTEM_H
#define SYSTEM_H

#define D_LOG_VERBOSE 1
#define D_LOG_DEBUG 2
#define D_LOG_WARN 3
#define D_LOG_ERROR 4

#include "Export.h"
#include <stdio.h>
#include <string>
#include <vector>
#include "io/Data.h"
#include "sokol_gfx.h"

namespace doriax {
    class Engine;

    // AdMob.h and InAppPurchase.h
    enum class AdMobFormat;
    enum class AdMobBannerSize;
    enum class AdMobBannerPosition;
    enum class AdMobRating;
    enum class AdMobAgeRestriction;
    enum class AdMobPersonalization;
    enum class AdMobConsentStatus;
    enum class AdMobDebugGeography;
    enum class ProductType;
    struct PurchaseParams;

    enum class CursorType{
        ARROW, // default
        IBEAM,
        CROSSHAIR,
        POINTING_HAND,
        RESIZE_EW,
        RESIZE_NS,
        RESIZE_NWSE,
        RESIZE_NESW,
        RESIZE_ALL,
        NOT_ALLOWED
    };

    // How the mouse cursor behaves. A single authoritative state replaces the old
    // independent "show cursor" and "mouse locked" flags: visibility and motion
    // confinement are two axes of one mode, so only the valid combinations exist.
    enum class MouseMode{
        NORMAL,   // visible, moves freely (default)
        HIDDEN,   // hidden, moves freely (absolute position still reported)
        CAPTURED, // hidden and locked to the window; relative motion only (mouse-look)
        CONFINED  // visible, but cannot leave the window bounds
    };

    class DORIAX_API System {
    private:

        friend class Engine;

        static System* systemInstance;

        // Only Engine is allowed to inject an external System implementation.
        static void setSystemInstance(System* system);

    protected:

        System() {}

    public:
        std::vector<std::string> args;

        static System& instance();

        virtual ~System() {}

        // *******
        // Used for user and Lua
        // *******
        virtual int getScreenWidth() = 0;
        virtual int getScreenHeight() = 0;

        virtual int getSampleCount();

        // Touch is the main input, as on phones and tablets; a laptop with a touch
        // screen and a mouse is not a touch device
        virtual bool isTouchDevice();

        virtual void showVirtualKeyboard(std::wstring text = L"");
        virtual void hideVirtualKeyboard();

        virtual bool isFullscreen();
        virtual void requestFullscreen();
        virtual void exitFullscreen();

        // Desktop window control. Maximize/resize/resizable are no-ops where
        // the platform offers no such control (mobile, web, sokol app backend);
        // setWindowTitle also applies on web (tab title) and sokol.
        // quit() closes the app, except on iOS where it is a no-op.
        virtual bool isWindowMaximized();
        virtual void maximizeWindow();
        virtual void restoreWindow();
        virtual void setWindowSize(int width, int height);
        virtual bool isWindowResizable();
        virtual void setWindowResizable(bool resizable);
        virtual void setWindowTitle(const std::string& title);
        virtual void quit();

        virtual char getDirSeparator();
        
        virtual std::string getAssetPath();
        virtual std::string getUserDataPath();
        virtual std::string getLuaPath();
        virtual std::string getShaderPath();
        // Where the editor keeps the models it imports; empty when each sits next to its source
        virtual std::string getImportPath();

        // *******
        // Used only for engine
        // *******
        virtual sg_environment getSokolEnvironment();
        virtual sg_swapchain getSokolSwapchain();

        virtual void setMouseCursor(CursorType type);
        virtual void setMouseMode(MouseMode mode);
        virtual void setMousePosition(float x, float y);

        virtual FILE* platformFopen(const char* fname, const char* mode);
        virtual bool syncFileSystem();

        virtual void platformLog(const int type, const char *fmt, va_list args);

        virtual bool getBoolForKey(const char *key, bool defaultValue);
        virtual int getIntegerForKey(const char *key, int defaultValue);
        virtual long getLongForKey(const char *key, long defaultValue);
        virtual float getFloatForKey(const char *key, float defaultValue);
        virtual double getDoubleForKey(const char *key, double defaultValue);
        virtual Data getDataForKey(const char *key, const Data& defaultValue);
        virtual std::string getStringForKey(const char *key, const std::string& defaultValue);

        virtual void setBoolForKey(const char *key, bool value);
        virtual void setIntegerForKey(const char *key, int value);
        virtual void setLongForKey(const char *key, long value);
        virtual void setFloatForKey(const char *key, float value);
        virtual void setDoubleForKey(const char *key, double value);
        virtual void setDataForKey(const char *key, Data& value);
        virtual void setStringForKey(const char* key, const std::string& value);

        virtual void removeKey(const char *key);

        // Used by the AdMob class; false where the platform or build has no AdMob
        virtual bool admobInitialize();
        virtual bool admobSetRequestConfiguration(AdMobRating rating, AdMobAgeRestriction ageRestriction, AdMobPersonalization personalization, const std::vector<std::string>& testDeviceIds);
        virtual bool admobRequestConsent(bool underAgeOfConsent, AdMobDebugGeography debugGeography, const std::vector<std::string>& testDeviceIds);
        virtual AdMobConsentStatus admobGetConsentStatus();
        virtual bool admobCanRequestAds();
        virtual bool admobIsPrivacyOptionsRequired();
        virtual bool admobShowPrivacyOptionsForm();
        virtual void admobResetConsent();
        // generation goes back with the load result
        virtual bool admobLoadAd(AdMobFormat format, const std::string& adUnitId, int generation);
        virtual bool admobShowAd(AdMobFormat format);
        virtual bool admobLoadBanner(const std::string& adUnitId, AdMobBannerSize size, AdMobBannerPosition position, bool visible, int generation);
        virtual void admobSetBannerVisible(bool visible);
        virtual void admobSetBannerPosition(AdMobBannerPosition position);
        virtual void admobRemoveBanner();
        virtual void admobSetServerSideVerificationOptions(const std::string& userId, const std::string& customData);
        virtual void admobSetAppVolume(float volume);
        virtual void admobSetAppMuted(bool muted);
        virtual bool admobOpenAdInspector();

        // Used by the InAppPurchase class; false where the platform or build has no billing
        virtual bool billingInitialize();
        virtual bool billingIsReady();
        virtual bool billingQueryProducts(const std::vector<std::string>& productIds, ProductType type);
        virtual bool billingPurchase(const PurchaseParams& params);
        virtual bool billingAcknowledgePurchase(const std::string& purchaseToken);
        virtual bool billingConsumePurchase(const std::string& purchaseToken);
        virtual bool billingQueryPurchases(ProductType type);
        virtual bool billingOpenSubscriptionManagement(const std::string& productId);
        virtual bool billingShowInAppMessages();

        // CrazyGames SDK
        virtual void initializeCrazyGamesSDK();
        virtual void showCrazyGamesAd(const std::string& type);
        virtual void happytimeCrazyGames();
        virtual void gameplayStartCrazyGames();
        virtual void gameplayStopCrazyGames();
        virtual void loadingStartCrazyGames();
        virtual void loadingStopCrazyGames();
    };

}


#endif //SYSTEM_H
