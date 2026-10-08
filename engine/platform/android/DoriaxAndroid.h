// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef DoriaxAndroid_H_
#define DoriaxAndroid_H_

#include <stdio.h>
#include <string>
#include "System.h"

class DoriaxAndroid: public doriax::System {

private:

    const char* logtag;

public:

    DoriaxAndroid();

    virtual int getScreenWidth() override;
    virtual int getScreenHeight() override;

    virtual bool isTouchDevice() override;

    virtual void showVirtualKeyboard(std::wstring text) override;
    virtual void hideVirtualKeyboard() override;

    virtual void quit() override;

    virtual std::string getUserDataPath() override;

    virtual FILE* platformFopen(const char* fname, const char* mode) override;
    virtual void platformLog(const int type, const char *fmt, va_list args) override;

    virtual bool getBoolForKey(const char *key, bool defaultValue) override;
    virtual int getIntegerForKey(const char *key, int defaultValue) override;
    virtual long getLongForKey(const char *key, long defaultValue) override;
    virtual float getFloatForKey(const char *key, float defaultValue) override;
    virtual double getDoubleForKey(const char *key, double defaultValue) override;
    virtual std::string getStringForKey(const char *key, const std::string& defaultValue) override;

    virtual void setBoolForKey(const char *key, bool value) override;
    virtual void setIntegerForKey(const char *key, int value) override;
    virtual void setLongForKey(const char *key, long value) override;
    virtual void setFloatForKey(const char *key, float value) override;
    virtual void setDoubleForKey(const char *key, double value) override;
    virtual void setStringForKey(const char* key, const std::string& value) override;

    virtual void removeKey(const char* key) override;

    // AndroidServices.cpp
    virtual bool admobInitialize() override;
    virtual bool admobSetRequestConfiguration(doriax::AdMobRating rating, doriax::AdMobAgeRestriction ageRestriction, doriax::AdMobPersonalization personalization, const std::vector<std::string>& testDeviceIds) override;
    virtual bool admobRequestConsent(bool underAgeOfConsent, doriax::AdMobDebugGeography debugGeography, const std::vector<std::string>& testDeviceIds) override;
    virtual doriax::AdMobConsentStatus admobGetConsentStatus() override;
    virtual bool admobCanRequestAds() override;
    virtual bool admobIsPrivacyOptionsRequired() override;
    virtual bool admobShowPrivacyOptionsForm() override;
    virtual void admobResetConsent() override;
    virtual bool admobLoadAd(doriax::AdMobFormat format, const std::string& adUnitId, int generation) override;
    virtual bool admobShowAd(doriax::AdMobFormat format) override;
    virtual bool admobLoadBanner(const std::string& adUnitId, doriax::AdMobBannerSize size, doriax::AdMobBannerPosition position, bool visible, int generation) override;
    virtual void admobSetBannerVisible(bool visible) override;
    virtual void admobSetBannerPosition(doriax::AdMobBannerPosition position) override;
    virtual void admobRemoveBanner() override;
    virtual void admobSetServerSideVerificationOptions(const std::string& userId, const std::string& customData) override;
    virtual void admobSetAppVolume(float volume) override;
    virtual void admobSetAppMuted(bool muted) override;
    virtual bool admobOpenAdInspector() override;

    virtual bool billingInitialize() override;
    virtual bool billingIsReady() override;
    virtual bool billingQueryProducts(const std::vector<std::string>& productIds, doriax::ProductType type) override;
    virtual bool billingPurchase(const doriax::PurchaseParams& params) override;
    virtual bool billingAcknowledgePurchase(const std::string& purchaseToken) override;
    virtual bool billingConsumePurchase(const std::string& purchaseToken) override;
    virtual bool billingQueryPurchases(doriax::ProductType type) override;
    virtual bool billingOpenSubscriptionManagement(const std::string& productId) override;
    virtual bool billingShowInAppMessages() override;
};

#endif /* DoriaxAndroid_H_ */
