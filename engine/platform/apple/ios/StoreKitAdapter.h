// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#import <Foundation/Foundation.h>

// StoreKit 2 for doriax::InAppPurchase, implemented in StoreKitAdapter.swift (this is its
// bridging header). The ints are its enums; products and purchases go back as JSON, as on Android.
@interface StoreKitAdapter : NSObject

- (void)initializeStore;
- (BOOL)isReady;
- (void)queryProducts:(nonnull NSArray<NSString *> *)productIds type:(int)type;
// accountToken is a UUID or empty
- (void)purchase:(nonnull NSString *)productId accountToken:(nonnull NSString *)accountToken;
// The token is the original transaction id
- (void)finishPurchase:(nonnull NSString *)purchaseToken consume:(BOOL)consume;
- (void)queryPurchases:(int)type;
// Syncs the App Store account, then queries both types
- (void)restorePurchases;
- (void)openSubscriptionManagement:(nonnull NSString *)productId;

@end

// Engine callbacks, implemented in DoriaxApple.mm
#ifdef __cplusplus
extern "C" {
#endif
void DoriaxStoreKitInitialized(int responseCode, const char *_Nonnull message);
void DoriaxStoreKitProductsQueried(int responseCode, const char *_Nonnull message, const char *_Nonnull productsJson);
void DoriaxStoreKitPurchaseUpdated(const char *_Nonnull purchaseJson);
void DoriaxStoreKitPurchaseFailed(const char *_Nonnull productId, int responseCode, const char *_Nonnull message);
void DoriaxStoreKitPurchasesQueried(int productType, int responseCode, const char *_Nonnull message, const char *_Nonnull purchasesJson);
void DoriaxStoreKitPurchaseAcknowledged(const char *_Nonnull purchaseToken, int responseCode, const char *_Nonnull message);
void DoriaxStoreKitPurchaseConsumed(const char *_Nonnull purchaseToken, int responseCode, const char *_Nonnull message);
#ifdef __cplusplus
}
#endif
