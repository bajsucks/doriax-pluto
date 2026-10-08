// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "InAppPurchase.h"

#include "Log.h"
#include "System.h"
#include <algorithm>

using namespace doriax;

namespace {
    const char* notAvailableMessage = "In-app purchases are not available on this platform or build";

    void warnNotAvailable(){
        static bool warned = false;
        if (!warned){
            Log::warn("%s. Android exports have them when Google Play Billing is enabled in the project settings.", notAvailableMessage);
            warned = true;
        }
    }

    InAppPurchaseBackend* getBackend(){
        return System::instance().getInAppPurchaseBackend();
    }
}

std::map<std::string, ProductDetails> InAppPurchase::products;
std::vector<PurchaseDetails> InAppPurchase::purchases;
std::string InAppPurchase::obfuscatedAccountId;
std::string InAppPurchase::obfuscatedProfileId;

std::mutex InAppPurchase::eventMutex;
std::vector<std::function<void()>> InAppPurchase::pendingEvents;

FunctionSubscribe<void(BillingResponse, std::string)> InAppPurchase::onInitialized;
FunctionSubscribe<void()> InAppPurchase::onDisconnected;
FunctionSubscribe<void(BillingResponse, std::string)> InAppPurchase::onProductsQueried;
FunctionSubscribe<void(PurchaseDetails)> InAppPurchase::onPurchaseUpdated;
FunctionSubscribe<void(std::string, BillingResponse, std::string)> InAppPurchase::onPurchaseFailed;
FunctionSubscribe<void(ProductType, BillingResponse, std::string)> InAppPurchase::onPurchasesQueried;
FunctionSubscribe<void(std::string, BillingResponse, std::string)> InAppPurchase::onPurchaseAcknowledged;
FunctionSubscribe<void(std::string, BillingResponse, std::string)> InAppPurchase::onPurchaseConsumed;

void InAppPurchase::postEvent(std::function<void()> event){
    std::lock_guard<std::mutex> lock(eventMutex);
    pendingEvents.push_back(std::move(event));
}

void InAppPurchase::dispatchEvents(){
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

void InAppPurchase::clearSubscriptions(){
    onInitialized.clear();
    onDisconnected.clear();
    onProductsQueried.clear();
    onPurchaseUpdated.clear();
    onPurchaseFailed.clear();
    onPurchasesQueried.clear();
    onPurchaseAcknowledged.clear();
    onPurchaseConsumed.clear();
}

void InAppPurchase::removeSubscriptionsByTag(const std::string& substring){
    onInitialized.removeByTagSubstring(substring);
    onDisconnected.removeByTagSubstring(substring);
    onProductsQueried.removeByTagSubstring(substring);
    onPurchaseUpdated.removeByTagSubstring(substring);
    onPurchaseFailed.removeByTagSubstring(substring);
    onPurchasesQueried.removeByTagSubstring(substring);
    onPurchaseAcknowledged.removeByTagSubstring(substring);
    onPurchaseConsumed.removeByTagSubstring(substring);
}

void InAppPurchase::reset(){
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        pendingEvents.clear();
    }

    products.clear();
    purchases.clear();
    obfuscatedAccountId.clear();
    obfuscatedProfileId.clear();
}

void InAppPurchase::initialize(){
    if (InAppPurchaseBackend* backend = getBackend()){
        backend->initialize();
    }else{
        warnNotAvailable();
        postEvent([](){ onInitialized.call(BillingResponse::BILLING_UNAVAILABLE, notAvailableMessage); });
    }
}

bool InAppPurchase::isReady(){
    InAppPurchaseBackend* backend = getBackend();
    return backend && backend->isReady();
}

void InAppPurchase::queryProducts(const std::vector<std::string>& productIds, ProductType type){
    if (productIds.empty()){
        postEvent([](){ onProductsQueried.call(BillingResponse::DEVELOPER_ERROR, "No product ids to query"); });
        return;
    }

    if (InAppPurchaseBackend* backend = getBackend()){
        backend->queryProducts(productIds, type);
    }else{
        warnNotAvailable();
        postEvent([](){ onProductsQueried.call(BillingResponse::BILLING_UNAVAILABLE, notAvailableMessage); });
    }
}

bool InAppPurchase::hasProduct(const std::string& productId){
    return products.find(productId) != products.end();
}

ProductDetails InAppPurchase::getProduct(const std::string& productId){
    auto it = products.find(productId);
    if (it == products.end()){
        return ProductDetails();
    }
    return it->second;
}

std::vector<ProductDetails> InAppPurchase::getProducts(){
    std::vector<ProductDetails> list;
    for (const auto& entry : products){
        list.push_back(entry.second);
    }
    return list;
}

void InAppPurchase::launchPurchase(const PurchaseParams& params){
    if (InAppPurchaseBackend* backend = getBackend()){
        backend->purchase(params);
    }else{
        warnNotAvailable();
        const std::string productId = params.productId;
        postEvent([productId](){ onPurchaseFailed.call(productId, BillingResponse::BILLING_UNAVAILABLE, notAvailableMessage); });
    }
}

void InAppPurchase::purchase(const std::string& productId, const std::string& offerToken){
    PurchaseParams params;
    params.productId = productId;
    params.offerToken = offerToken;
    params.obfuscatedAccountId = obfuscatedAccountId;
    params.obfuscatedProfileId = obfuscatedProfileId;

    launchPurchase(params);
}

void InAppPurchase::changeSubscription(const std::string& productId, const std::string& offerToken, const std::string& oldPurchaseToken, SubscriptionReplacementMode mode){
    auto old = std::find_if(purchases.begin(), purchases.end(), [&](const PurchaseDetails& purchase){
        return purchase.purchaseToken == oldPurchaseToken;
    });
    if (old == purchases.end()){
        postEvent([productId](){
            onPurchaseFailed.call(productId, BillingResponse::DEVELOPER_ERROR, "The subscription to replace is not owned. Query subscription purchases first");
        });
        return;
    }

    PurchaseParams params;
    params.productId = productId;
    params.offerToken = offerToken;
    params.oldPurchaseToken = oldPurchaseToken;
    params.oldProductId = old->productId;
    params.replacementMode = mode;
    params.obfuscatedAccountId = obfuscatedAccountId;
    params.obfuscatedProfileId = obfuscatedProfileId;

    launchPurchase(params);
}

void InAppPurchase::acknowledgePurchase(const std::string& purchaseToken){
    if (InAppPurchaseBackend* backend = getBackend()){
        backend->acknowledgePurchase(purchaseToken);
    }else{
        warnNotAvailable();
        postEvent([purchaseToken](){ onPurchaseAcknowledged.call(purchaseToken, BillingResponse::BILLING_UNAVAILABLE, notAvailableMessage); });
    }
}

void InAppPurchase::consumePurchase(const std::string& purchaseToken){
    if (InAppPurchaseBackend* backend = getBackend()){
        backend->consumePurchase(purchaseToken);
    }else{
        warnNotAvailable();
        postEvent([purchaseToken](){ onPurchaseConsumed.call(purchaseToken, BillingResponse::BILLING_UNAVAILABLE, notAvailableMessage); });
    }
}

void InAppPurchase::queryPurchases(ProductType type){
    if (InAppPurchaseBackend* backend = getBackend()){
        backend->queryPurchases(type);
    }else{
        warnNotAvailable();
        postEvent([type](){ onPurchasesQueried.call(type, BillingResponse::BILLING_UNAVAILABLE, notAvailableMessage); });
    }
}

std::vector<PurchaseDetails> InAppPurchase::getPurchases(){
    return purchases;
}

bool InAppPurchase::isPurchased(const std::string& productId){
    for (const PurchaseDetails& purchase : purchases){
        if (purchase.state != PurchaseState::PURCHASED || purchase.suspended)
            continue;
        if (std::find(purchase.productIds.begin(), purchase.productIds.end(), productId) != purchase.productIds.end())
            return true;
    }
    return false;
}

void InAppPurchase::setObfuscatedAccountId(const std::string& accountId){
    obfuscatedAccountId = accountId;
}

void InAppPurchase::setObfuscatedProfileId(const std::string& profileId){
    obfuscatedProfileId = profileId;
}

void InAppPurchase::openSubscriptionManagement(const std::string& productId){
    if (InAppPurchaseBackend* backend = getBackend()){
        backend->openSubscriptionManagement(productId);
    }else{
        warnNotAvailable();
    }
}

void InAppPurchase::showInAppMessages(){
    if (InAppPurchaseBackend* backend = getBackend()){
        backend->showInAppMessages();
    }else{
        warnNotAvailable();
    }
}

void InAppPurchase::storePurchase(const PurchaseDetails& purchase){
    for (PurchaseDetails& stored : purchases){
        if (stored.purchaseToken == purchase.purchaseToken){
            stored = purchase;
            return;
        }
    }
    purchases.push_back(purchase);
}

void InAppPurchase::systemInitialized(BillingResponse response, const std::string& message){
    postEvent([response, message](){ onInitialized.call(response, message); });
}

void InAppPurchase::systemDisconnected(){
    postEvent([](){ onDisconnected.call(); });
}

void InAppPurchase::systemProductsQueried(BillingResponse response, const std::string& message, const std::vector<ProductDetails>& queried){
    postEvent([response, message, queried](){
        for (const ProductDetails& product : queried){
            products[product.productId] = product;
        }
        onProductsQueried.call(response, message);
    });
}

void InAppPurchase::systemPurchaseUpdated(const PurchaseDetails& purchase){
    postEvent([purchase](){
        storePurchase(purchase);
        onPurchaseUpdated.call(purchase);
    });
}

void InAppPurchase::systemPurchaseFailed(const std::string& productId, BillingResponse response, const std::string& message){
    postEvent([productId, response, message](){ onPurchaseFailed.call(productId, response, message); });
}

void InAppPurchase::systemPurchasesQueried(ProductType type, BillingResponse response, const std::string& message, const std::vector<PurchaseDetails>& owned){
    postEvent([type, response, message, owned](){
        if (response == BillingResponse::OK){
            // the store's answer replaces this type: refunded and expired purchases go away
            purchases.erase(std::remove_if(purchases.begin(), purchases.end(), [type](const PurchaseDetails& purchase){
                return purchase.productType == type;
            }), purchases.end());

            std::vector<PurchaseDetails> restored = owned;
            for (PurchaseDetails& purchase : restored){
                purchase.productType = type;
                purchase.restored = true;
                storePurchase(purchase);
            }
            for (const PurchaseDetails& purchase : restored){
                onPurchaseUpdated.call(purchase);
            }
        }
        onPurchasesQueried.call(type, response, message);
    });
}

void InAppPurchase::systemPurchaseAcknowledged(const std::string& purchaseToken, BillingResponse response, const std::string& message){
    postEvent([purchaseToken, response, message](){
        if (response == BillingResponse::OK){
            for (PurchaseDetails& purchase : purchases){
                if (purchase.purchaseToken == purchaseToken){
                    purchase.acknowledged = true;
                }
            }
        }
        onPurchaseAcknowledged.call(purchaseToken, response, message);
    });
}

void InAppPurchase::systemPurchaseConsumed(const std::string& purchaseToken, BillingResponse response, const std::string& message){
    postEvent([purchaseToken, response, message](){
        if (response == BillingResponse::OK){
            // a consumed product is no longer owned
            purchases.erase(std::remove_if(purchases.begin(), purchases.end(), [&purchaseToken](const PurchaseDetails& purchase){
                return purchase.purchaseToken == purchaseToken;
            }), purchases.end());
        }
        onPurchaseConsumed.call(purchaseToken, response, message);
    });
}
