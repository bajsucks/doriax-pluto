// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef INAPPPURCHASE_H
#define INAPPPURCHASE_H

#include "Export.h"
#include "util/FunctionSubscribe.h"
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace doriax {

    // Sent to the platform as integers, so keep the order
    enum class ProductType{
        INAPP, // one-time product: consumable or not
        SUBS
    };

    enum class PurchaseState{
        UNSPECIFIED,
        PURCHASED,
        PENDING // waiting for payment: do not grant yet
    };

    // Google Play Billing response codes
    enum class BillingResponse{
        SERVICE_TIMEOUT = -3,
        FEATURE_NOT_SUPPORTED = -2,
        SERVICE_DISCONNECTED = -1,
        OK = 0,
        USER_CANCELED = 1,
        SERVICE_UNAVAILABLE = 2,
        BILLING_UNAVAILABLE = 3,
        ITEM_UNAVAILABLE = 4,
        DEVELOPER_ERROR = 5,
        FATAL_ERROR = 6, // ERROR in Google Play Billing
        ITEM_ALREADY_OWNED = 7,
        ITEM_NOT_OWNED = 8,
        NETWORK_ERROR = 12
    };

    enum class RecurrenceMode{
        INFINITE_RECURRING,
        FINITE_RECURRING,
        NON_RECURRING
    };

    // How a subscription change bills the remaining time of the old plan
    enum class SubscriptionReplacementMode{
        WITH_TIME_PRORATION,
        CHARGE_PRORATED_PRICE,
        WITHOUT_PRORATION,
        CHARGE_FULL_PRICE,
        DEFERRED,
        KEEP_EXISTING
    };

    struct PricingPhase{
        std::string price; // formatted with the currency symbol
        long long priceMicros = 0;
        std::string currencyCode;
        std::string billingPeriod; // ISO 8601 (P1M), empty for one-time products
        int billingCycleCount = 0;
        RecurrenceMode recurrenceMode = RecurrenceMode::NON_RECURRING;
    };

    // A subscription base plan or offer, or a purchase option of a one-time product
    struct ProductOffer{
        std::string offerToken;
        std::string offerId; // empty for a base plan
        std::string basePlanId; // purchase option id for one-time products
        std::vector<std::string> tags;
        std::vector<PricingPhase> pricingPhases;
    };

    struct ProductDetails{
        std::string productId;
        ProductType type = ProductType::INAPP;
        std::string title;
        std::string name;
        std::string description;
        // For a subscription, the recurring price of the plan bought without an offer token
        std::string price;
        long long priceMicros = 0;
        std::string currencyCode;
        std::vector<ProductOffer> offers;
    };

    struct PurchaseDetails{
        std::string orderId;
        std::string productId;
        std::vector<std::string> productIds;
        ProductType productType = ProductType::INAPP;
        std::string purchaseToken;
        long long purchaseTime = 0; // milliseconds since the epoch
        PurchaseState state = PurchaseState::UNSPECIFIED;
        int quantity = 1;
        bool acknowledged = false;
        bool autoRenewing = false;
        bool suspended = false; // subscription on hold for a payment problem: do not grant
        bool restored = false; // reported by queryPurchases, not by a purchase flow
        std::string packageName;
        std::string obfuscatedAccountId;
        std::string obfuscatedProfileId;
        // Signed purchase data, to verify on a server
        std::string originalJson;
        std::string signature;
    };

    // What InAppPurchase gives the platform to launch a purchase
    struct PurchaseParams{
        std::string productId;
        std::string offerToken;
        std::string oldPurchaseToken;
        std::string oldProductId;
        SubscriptionReplacementMode replacementMode = SubscriptionReplacementMode::WITH_TIME_PRORATION;
        std::string obfuscatedAccountId;
        std::string obfuscatedProfileId;
    };

    // What a platform with an app store implements, see System::getInAppPurchaseBackend.
    // Called from the engine thread, it answers through the InAppPurchase::system* callbacks.
    class DORIAX_API InAppPurchaseBackend {
    public:
        virtual ~InAppPurchaseBackend() = default;

        virtual void initialize() = 0;
        virtual bool isReady() = 0;
        virtual void queryProducts(const std::vector<std::string>& productIds, ProductType type) = 0;
        virtual void purchase(const PurchaseParams& params) = 0;
        virtual void acknowledgePurchase(const std::string& purchaseToken) = 0;
        virtual void consumePurchase(const std::string& purchaseToken) = 0;
        virtual void queryPurchases(ProductType type) = 0;
        virtual void openSubscriptionManagement(const std::string& productId) = 0;
        virtual void showInAppMessages() = 0;
    };

    // Google Play Billing on Android. Results arrive as events at the start of a frame.
    // Acknowledge or consume a purchase within three days, or Google Play refunds it.
    class DORIAX_API InAppPurchase {

        friend class Engine;

    private:
        static std::map<std::string, ProductDetails> products;
        static std::vector<PurchaseDetails> purchases;
        static std::string obfuscatedAccountId;
        static std::string obfuscatedProfileId;

        static std::mutex eventMutex;
        static std::vector<std::function<void()>> pendingEvents;

        static void postEvent(std::function<void()> event);
        static void launchPurchase(const PurchaseParams& params);
        static void storePurchase(const PurchaseDetails& purchase);

        // Called by Engine
        static void dispatchEvents();
        static void clearSubscriptions();
        static void removeSubscriptionsByTag(const std::string& substring);
        static void reset();

    public:
        // Calls before onInitialized reports OK fail with SERVICE_DISCONNECTED
        static void initialize();
        static bool isReady();

        // Results go to getProducts and getProduct
        static void queryProducts(const std::vector<std::string>& productIds, ProductType type);
        static bool hasProduct(const std::string& productId);
        static ProductDetails getProduct(const std::string& productId);
        static std::vector<ProductDetails> getProducts();

        // Needs the product queried first. An empty offerToken buys the first base plan.
        static void purchase(const std::string& productId, const std::string& offerToken = "");
        // KEEP_EXISTING carries the old plan's payments over and ignores offerToken
        static void changeSubscription(const std::string& productId, const std::string& offerToken, const std::string& oldPurchaseToken, SubscriptionReplacementMode mode = SubscriptionReplacementMode::WITH_TIME_PRORATION);

        static void acknowledgePurchase(const std::string& purchaseToken);
        // Consuming also acknowledges, and lets the product be bought again
        static void consumePurchase(const std::string& purchaseToken);

        // Reports owned purchases with onPurchaseUpdated, then onPurchasesQueried.
        // Call it at startup and on resume.
        static void queryPurchases(ProductType type);
        static std::vector<PurchaseDetails> getPurchases();
        static bool isPurchased(const std::string& productId);

        // Hashed user ids that Google Play uses against fraud
        static void setObfuscatedAccountId(const std::string& accountId);
        static void setObfuscatedProfileId(const std::string& profileId);

        // Google Play subscription center, for one subscription or all of them
        static void openSubscriptionManagement(const std::string& productId = "");
        // Messages about subscription payment problems
        static void showInAppMessages();

        static FunctionSubscribe<void(BillingResponse, std::string)> onInitialized;
        static FunctionSubscribe<void()> onDisconnected;
        static FunctionSubscribe<void(BillingResponse, std::string)> onProductsQueried;
        static FunctionSubscribe<void(PurchaseDetails)> onPurchaseUpdated;
        // productId of the failed purchase flow, empty when unknown
        static FunctionSubscribe<void(std::string, BillingResponse, std::string)> onPurchaseFailed;
        static FunctionSubscribe<void(ProductType, BillingResponse, std::string)> onPurchasesQueried;
        static FunctionSubscribe<void(std::string, BillingResponse, std::string)> onPurchaseAcknowledged;
        static FunctionSubscribe<void(std::string, BillingResponse, std::string)> onPurchaseConsumed;

        // Platform callbacks, safe from any thread
        static void systemInitialized(BillingResponse response, const std::string& message);
        static void systemDisconnected();
        static void systemProductsQueried(BillingResponse response, const std::string& message, const std::vector<ProductDetails>& queried);
        static void systemPurchaseUpdated(const PurchaseDetails& purchase);
        static void systemPurchaseFailed(const std::string& productId, BillingResponse response, const std::string& message);
        static void systemPurchasesQueried(ProductType type, BillingResponse response, const std::string& message, const std::vector<PurchaseDetails>& owned);
        static void systemPurchaseAcknowledged(const std::string& purchaseToken, BillingResponse response, const std::string& message);
        static void systemPurchaseConsumed(const std::string& purchaseToken, BillingResponse response, const std::string& message);
    };

}

#endif //INAPPPURCHASE_H
