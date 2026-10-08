// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#import <Foundation/Foundation.h>

// Google Mobile Ads and UMP for doriax::AdMob; the ints are its enums
@interface AdMobAdapter : NSObject

- (void)initializeAdMob;
- (void)setRequestConfigurationWithRating:(int)rating ageRestriction:(int)ageRestriction personalization:(int)personalization testDeviceIds:(nonnull NSArray<NSString *> *)testDeviceIds;

- (void)requestConsentUnderAge:(BOOL)underAgeOfConsent debugGeography:(int)debugGeography testDeviceIds:(nonnull NSArray<NSString *> *)testDeviceIds;
- (int)consentStatus;
- (BOOL)canRequestAds;
- (BOOL)isPrivacyOptionsRequired;
- (void)showPrivacyOptionsForm;
- (void)resetConsent;

// generation goes back with the load result
- (void)loadAd:(int)format adUnitId:(nonnull NSString *)adUnitId generation:(int)generation;
- (void)showAd:(int)format;

- (void)loadBanner:(nonnull NSString *)adUnitId size:(int)size position:(int)position visible:(BOOL)visible generation:(int)generation;
- (void)setBannerVisible:(BOOL)visible;
- (void)setBannerPosition:(int)position;
- (void)removeBanner;

- (void)setServerSideVerificationUserId:(nonnull NSString *)userId customData:(nonnull NSString *)customData;
- (void)setAppVolume:(float)volume;
- (void)setAppMuted:(BOOL)muted;
- (void)openAdInspector;

@end

// Engine callbacks, implemented in DoriaxApple.mm
#ifdef __cplusplus
extern "C" {
#endif
void DoriaxAdMobInitialized(void);
void DoriaxAdMobConsentUpdated(int errorCode, const char *_Nonnull message);
void DoriaxAdMobAdLoaded(int format, int generation, int width, int height);
void DoriaxAdMobAdFailedToLoad(int format, int generation, int errorCode, const char *_Nonnull message);
void DoriaxAdMobAdShown(int format);
void DoriaxAdMobAdFailedToShow(int format, int errorCode, const char *_Nonnull message);
void DoriaxAdMobAdDismissed(int format);
void DoriaxAdMobAdClicked(int format);
void DoriaxAdMobAdImpression(int format);
void DoriaxAdMobAdPaid(int format, long long valueMicros, const char *_Nonnull currencyCode, int precision);
void DoriaxAdMobUserEarnedReward(int format, const char *_Nonnull type, int amount);
void DoriaxAdMobAdInspectorClosed(int errorCode, const char *_Nonnull message);
#ifdef __cplusplus
}
#endif
