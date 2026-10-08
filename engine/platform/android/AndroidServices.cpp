// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

// DoriaxAndroid's AdMob and Google Play Billing hooks, and the natives of their Java wrappers

#include "AndroidServices.h"

#include "DoriaxAndroid.h"
#include "NativeEngine.h"
#include "service/AdMob.h"
#include "service/InAppPurchase.h"
#include "Log.h"
#include "util/StringUtils.h"
#include "json.hpp"

#include <cstdint>

using namespace doriax;

namespace {

    // A pending Java exception would abort the next JNI call
    bool clearException(JNIEnv* env){
        if (env->ExceptionCheck()){
            env->ExceptionDescribe();
            env->ExceptionClear();
            return true;
        }
        return false;
    }

    // Java strings are UTF-16; GetStringUTFChars gives modified UTF-8, which breaks emoji
    std::string toString(JNIEnv* env, jstring text){
        if (!text)
            return "";

        const jsize length = env->GetStringLength(text);
        const jchar* chars = env->GetStringChars(text, nullptr);
        if (!chars)
            return "";

        std::string utf8;
        for (jsize i = 0; i < length; i++){
            uint32_t code = chars[i];
            if (code >= 0xD800 && code <= 0xDBFF && i + 1 < length && chars[i + 1] >= 0xDC00 && chars[i + 1] <= 0xDFFF){
                code = 0x10000 + ((code - 0xD800) << 10) + (chars[++i] - 0xDC00);
            }
            utf8 += StringUtils::toUTF8(static_cast<wchar_t>(code));
        }
        env->ReleaseStringChars(text, chars);

        return utf8;
    }

    // NewStringUTF takes modified UTF-8 and rejects 4-byte sequences, so build UTF-16
    jstring toJavaString(JNIEnv* env, const std::string& text){
        bool hadInvalid;
        std::u16string utf16;
        for (uint32_t code : StringUtils::decodeUtf8ToCodepoints(text, hadInvalid)){
            if (code >= 0x10000){
                code -= 0x10000;
                utf16 += static_cast<char16_t>(0xD800 + (code >> 10));
                utf16 += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
            }else{
                utf16 += static_cast<char16_t>(code);
            }
        }
        return env->NewString(reinterpret_cast<const jchar*>(utf16.data()), static_cast<jsize>(utf16.size()));
    }

    class JavaString{
    public:
        JavaString(JNIEnv* env, const std::string& text): env(env), value(toJavaString(env, text)) {}
        ~JavaString(){ if (value) env->DeleteLocalRef(value); }
        JavaString(const JavaString&) = delete;
        JavaString& operator=(const JavaString&) = delete;

        jstring get() const { return value; }

    private:
        JNIEnv* env;
        jstring value;
    };

    class JavaStringArray{
    public:
        JavaStringArray(JNIEnv* env, const std::vector<std::string>& values): env(env) {
            jclass stringClass = env->FindClass("java/lang/String");
            value = env->NewObjectArray(static_cast<jsize>(values.size()), stringClass, nullptr);
            for (size_t i = 0; value && i < values.size(); i++){
                jstring item = toJavaString(env, values[i]);
                env->SetObjectArrayElement(value, static_cast<jsize>(i), item);
                env->DeleteLocalRef(item);
            }
            env->DeleteLocalRef(stringClass);
        }
        ~JavaStringArray(){ if (value) env->DeleteLocalRef(value); }
        JavaStringArray(const JavaStringArray&) = delete;
        JavaStringArray& operator=(const JavaStringArray&) = delete;

        jobjectArray get() const { return value; }

    private:
        JNIEnv* env;
        jobjectArray value = nullptr;
    };

    JniData& jniData(){
        return NativeEngine::getInstance()->getJniData();
    }

    JNIEnv* jniEnv(){
        return NativeEngine::getInstance()->getJniEnv();
    }

    // False when the export left the wrapper's service out
    template<typename... Args>
    bool callVoid(jobject wrapper, jmethodID method, Args... args){
        if (!wrapper)
            return false;

        JNIEnv* env = jniEnv();
        env->CallVoidMethod(wrapper, method, args...);
        clearException(env);
        return true;
    }

    using Json = nlohmann::json;

    std::string jsonString(const Json& json, const char* key){
        auto it = json.find(key);
        return (it != json.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }

    long long jsonInteger(const Json& json, const char* key){
        auto it = json.find(key);
        return (it != json.end() && it->is_number_integer()) ? it->get<long long>() : 0;
    }

    bool jsonBool(const Json& json, const char* key){
        auto it = json.find(key);
        return it != json.end() && it->is_boolean() && it->get<bool>();
    }

    std::vector<std::string> jsonStrings(const Json& json, const char* key){
        std::vector<std::string> values;
        auto it = json.find(key);
        if (it != json.end() && it->is_array()){
            for (const Json& item : *it){
                if (item.is_string()) values.push_back(item.get<std::string>());
            }
        }
        return values;
    }

    ProductDetails parseProduct(const Json& json){
        ProductDetails product;
        product.productId = jsonString(json, "productId");
        product.type = static_cast<ProductType>(jsonInteger(json, "type"));
        product.title = jsonString(json, "title");
        product.name = jsonString(json, "name");
        product.description = jsonString(json, "description");
        product.price = jsonString(json, "price");
        product.priceMicros = jsonInteger(json, "priceMicros");
        product.currencyCode = jsonString(json, "currencyCode");

        auto offers = json.find("offers");
        if (offers != json.end() && offers->is_array()){
            for (const Json& offerJson : *offers){
                ProductOffer offer;
                offer.offerToken = jsonString(offerJson, "offerToken");
                offer.offerId = jsonString(offerJson, "offerId");
                offer.basePlanId = jsonString(offerJson, "basePlanId");
                offer.tags = jsonStrings(offerJson, "tags");

                auto phases = offerJson.find("pricingPhases");
                if (phases != offerJson.end() && phases->is_array()){
                    for (const Json& phaseJson : *phases){
                        PricingPhase phase;
                        phase.price = jsonString(phaseJson, "price");
                        phase.priceMicros = jsonInteger(phaseJson, "priceMicros");
                        phase.currencyCode = jsonString(phaseJson, "currencyCode");
                        phase.billingPeriod = jsonString(phaseJson, "billingPeriod");
                        phase.billingCycleCount = static_cast<int>(jsonInteger(phaseJson, "billingCycleCount"));
                        phase.recurrenceMode = static_cast<RecurrenceMode>(jsonInteger(phaseJson, "recurrenceMode"));
                        offer.pricingPhases.push_back(phase);
                    }
                }
                product.offers.push_back(offer);
            }
        }

        return product;
    }

    PurchaseDetails parsePurchase(const Json& json){
        PurchaseDetails purchase;
        purchase.orderId = jsonString(json, "orderId");
        purchase.productIds = jsonStrings(json, "productIds");
        purchase.productId = purchase.productIds.empty() ? "" : purchase.productIds[0];
        purchase.productType = static_cast<ProductType>(jsonInteger(json, "productType"));
        purchase.purchaseToken = jsonString(json, "purchaseToken");
        purchase.purchaseTime = jsonInteger(json, "purchaseTime");
        purchase.state = static_cast<PurchaseState>(jsonInteger(json, "state"));
        purchase.quantity = static_cast<int>(jsonInteger(json, "quantity"));
        purchase.acknowledged = jsonBool(json, "acknowledged");
        purchase.autoRenewing = jsonBool(json, "autoRenewing");
        purchase.suspended = jsonBool(json, "suspended");
        purchase.packageName = jsonString(json, "packageName");
        purchase.obfuscatedAccountId = jsonString(json, "obfuscatedAccountId");
        purchase.obfuscatedProfileId = jsonString(json, "obfuscatedProfileId");
        purchase.originalJson = jsonString(json, "originalJson");
        purchase.signature = jsonString(json, "signature");
        return purchase;
    }

    Json parseJson(JNIEnv* env, jstring text){
        Json json = Json::parse(toString(env, text), nullptr, false);
        if (json.is_discarded()){
            Log::error("Google Play Billing sent unreadable data");
        }
        return json;
    }

    // AdMobWrapper natives

    void JNICALL admobOnInitialized(JNIEnv* env, jclass cls){
        AdMob::systemInitialized();
    }

    void JNICALL admobOnConsentUpdated(JNIEnv* env, jclass cls, jint errorCode, jstring message){
        AdMob::systemConsentUpdated(errorCode, toString(env, message));
    }

    void JNICALL admobOnAdLoaded(JNIEnv* env, jclass cls, jint format, jint generation, jint width, jint height){
        AdMob::systemAdLoaded(static_cast<AdMobFormat>(format), generation, width, height);
    }

    void JNICALL admobOnAdFailedToLoad(JNIEnv* env, jclass cls, jint format, jint generation, jint errorCode, jstring message){
        AdMob::systemAdFailedToLoad(static_cast<AdMobFormat>(format), generation, errorCode, toString(env, message));
    }

    void JNICALL admobOnAdShown(JNIEnv* env, jclass cls, jint format){
        AdMob::systemAdShown(static_cast<AdMobFormat>(format));
    }

    void JNICALL admobOnAdFailedToShow(JNIEnv* env, jclass cls, jint format, jint errorCode, jstring message){
        AdMob::systemAdFailedToShow(static_cast<AdMobFormat>(format), errorCode, toString(env, message));
    }

    void JNICALL admobOnAdDismissed(JNIEnv* env, jclass cls, jint format){
        AdMob::systemAdDismissed(static_cast<AdMobFormat>(format));
    }

    void JNICALL admobOnAdClicked(JNIEnv* env, jclass cls, jint format){
        AdMob::systemAdClicked(static_cast<AdMobFormat>(format));
    }

    void JNICALL admobOnAdImpression(JNIEnv* env, jclass cls, jint format){
        AdMob::systemAdImpression(static_cast<AdMobFormat>(format));
    }

    void JNICALL admobOnAdPaid(JNIEnv* env, jclass cls, jint format, jlong valueMicros, jstring currencyCode, jint precision){
        AdMob::systemAdPaid(static_cast<AdMobFormat>(format), static_cast<long long>(valueMicros), toString(env, currencyCode), static_cast<AdMobPrecision>(precision));
    }

    void JNICALL admobOnUserEarnedReward(JNIEnv* env, jclass cls, jint format, jstring type, jint amount){
        AdMob::systemUserEarnedReward(static_cast<AdMobFormat>(format), toString(env, type), amount);
    }

    void JNICALL admobOnAdInspectorClosed(JNIEnv* env, jclass cls, jint errorCode, jstring message){
        AdMob::systemAdInspectorClosed(errorCode, toString(env, message));
    }

    const JNINativeMethod admobNatives[] = {
        {"nativeOnInitialized", "()V", reinterpret_cast<void*>(admobOnInitialized)},
        {"nativeOnConsentUpdated", "(ILjava/lang/String;)V", reinterpret_cast<void*>(admobOnConsentUpdated)},
        {"nativeOnAdLoaded", "(IIII)V", reinterpret_cast<void*>(admobOnAdLoaded)},
        {"nativeOnAdFailedToLoad", "(IIILjava/lang/String;)V", reinterpret_cast<void*>(admobOnAdFailedToLoad)},
        {"nativeOnAdShown", "(I)V", reinterpret_cast<void*>(admobOnAdShown)},
        {"nativeOnAdFailedToShow", "(IILjava/lang/String;)V", reinterpret_cast<void*>(admobOnAdFailedToShow)},
        {"nativeOnAdDismissed", "(I)V", reinterpret_cast<void*>(admobOnAdDismissed)},
        {"nativeOnAdClicked", "(I)V", reinterpret_cast<void*>(admobOnAdClicked)},
        {"nativeOnAdImpression", "(I)V", reinterpret_cast<void*>(admobOnAdImpression)},
        {"nativeOnAdPaid", "(IJLjava/lang/String;I)V", reinterpret_cast<void*>(admobOnAdPaid)},
        {"nativeOnUserEarnedReward", "(ILjava/lang/String;I)V", reinterpret_cast<void*>(admobOnUserEarnedReward)},
        {"nativeOnAdInspectorClosed", "(ILjava/lang/String;)V", reinterpret_cast<void*>(admobOnAdInspectorClosed)},
    };

    // BillingWrapper natives

    void JNICALL billingOnInitialized(JNIEnv* env, jclass cls, jint responseCode, jstring message){
        InAppPurchase::systemInitialized(static_cast<BillingResponse>(responseCode), toString(env, message));
    }

    void JNICALL billingOnDisconnected(JNIEnv* env, jclass cls){
        InAppPurchase::systemDisconnected();
    }

    void JNICALL billingOnProductsQueried(JNIEnv* env, jclass cls, jint responseCode, jstring message, jstring productsJson){
        std::vector<ProductDetails> products;
        Json json = parseJson(env, productsJson);
        if (json.is_array()){
            for (const Json& item : json){
                if (item.is_object()) products.push_back(parseProduct(item));
            }
        }
        InAppPurchase::systemProductsQueried(static_cast<BillingResponse>(responseCode), toString(env, message), products);
    }

    void JNICALL billingOnPurchaseUpdated(JNIEnv* env, jclass cls, jstring purchaseJson){
        Json json = parseJson(env, purchaseJson);
        if (json.is_object()){
            InAppPurchase::systemPurchaseUpdated(parsePurchase(json));
        }
    }

    void JNICALL billingOnPurchaseFailed(JNIEnv* env, jclass cls, jstring productId, jint responseCode, jstring message){
        InAppPurchase::systemPurchaseFailed(toString(env, productId), static_cast<BillingResponse>(responseCode), toString(env, message));
    }

    void JNICALL billingOnPurchasesQueried(JNIEnv* env, jclass cls, jint productType, jint responseCode, jstring message, jstring purchasesJson){
        std::vector<PurchaseDetails> purchases;
        Json json = parseJson(env, purchasesJson);
        if (json.is_array()){
            for (const Json& item : json){
                if (item.is_object()) purchases.push_back(parsePurchase(item));
            }
        }
        InAppPurchase::systemPurchasesQueried(static_cast<ProductType>(productType), static_cast<BillingResponse>(responseCode), toString(env, message), purchases);
    }

    void JNICALL billingOnPurchaseAcknowledged(JNIEnv* env, jclass cls, jstring purchaseToken, jint responseCode, jstring message){
        InAppPurchase::systemPurchaseAcknowledged(toString(env, purchaseToken), static_cast<BillingResponse>(responseCode), toString(env, message));
    }

    void JNICALL billingOnPurchaseConsumed(JNIEnv* env, jclass cls, jstring purchaseToken, jint responseCode, jstring message){
        InAppPurchase::systemPurchaseConsumed(toString(env, purchaseToken), static_cast<BillingResponse>(responseCode), toString(env, message));
    }

    const JNINativeMethod billingNatives[] = {
        {"nativeOnInitialized", "(ILjava/lang/String;)V", reinterpret_cast<void*>(billingOnInitialized)},
        {"nativeOnDisconnected", "()V", reinterpret_cast<void*>(billingOnDisconnected)},
        {"nativeOnProductsQueried", "(ILjava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void*>(billingOnProductsQueried)},
        {"nativeOnPurchaseUpdated", "(Ljava/lang/String;)V", reinterpret_cast<void*>(billingOnPurchaseUpdated)},
        {"nativeOnPurchaseFailed", "(Ljava/lang/String;ILjava/lang/String;)V", reinterpret_cast<void*>(billingOnPurchaseFailed)},
        {"nativeOnPurchasesQueried", "(IILjava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void*>(billingOnPurchasesQueried)},
        {"nativeOnPurchaseAcknowledged", "(Ljava/lang/String;ILjava/lang/String;)V", reinterpret_cast<void*>(billingOnPurchaseAcknowledged)},
        {"nativeOnPurchaseConsumed", "(Ljava/lang/String;ILjava/lang/String;)V", reinterpret_cast<void*>(billingOnPurchaseConsumed)},
    };

    // The wrapper getter returns null when the export left the service out
    jobject findWrapper(JNIEnv* env, jobject activity, jclass activityClass, const char* getter){
        jmethodID method = env->GetMethodID(activityClass, getter, "()Ljava/lang/Object;");
        if (clearException(env) || !method)
            return nullptr;

        jobject wrapper = env->CallObjectMethod(activity, method);
        if (clearException(env))
            return nullptr;

        return wrapper;
    }

    bool registerNatives(JNIEnv* env, jclass cls, const JNINativeMethod* methods, int count){
        const bool registered = env->RegisterNatives(cls, methods, count) == JNI_OK;
        return !clearException(env) && registered;
    }

    // Stops at the first missing method: JNI must not run with an exception pending
    class MethodFinder{
    public:
        MethodFinder(JNIEnv* env, jclass cls): env(env), cls(cls) {}

        jmethodID find(const char* name, const char* signature){
            if (failed) return nullptr;
            jmethodID method = env->GetMethodID(cls, name, signature);
            if (clearException(env) || !method){
                Log::error("Java wrapper has no method %s %s", name, signature);
                failed = true;
                return nullptr;
            }
            return method;
        }

        bool ok() const { return !failed; }

    private:
        JNIEnv* env;
        jclass cls;
        bool failed = false;
    };
}

void setupServicesJNI(JNIEnv* env, JniData& jni){
    jni.adMobWrapperObjRef = nullptr;
    jni.billingWrapperObjRef = nullptr;

    if (jobject wrapper = findWrapper(env, jni.gameActivityObjRef, jni.gameActivityClsRef, "getAdMobWrapper")){
        jclass cls = env->GetObjectClass(wrapper);
        MethodFinder methods(env, cls);

        jni.admobInitialize = methods.find("initialize", "()V");
        jni.admobSetRequestConfiguration = methods.find("setRequestConfiguration", "(III[Ljava/lang/String;)V");
        jni.admobRequestConsent = methods.find("requestConsent", "(ZI[Ljava/lang/String;)V");
        jni.admobGetConsentStatus = methods.find("getConsentStatus", "()I");
        jni.admobCanRequestAds = methods.find("canRequestAds", "()Z");
        jni.admobIsPrivacyOptionsRequired = methods.find("isPrivacyOptionsRequired", "()Z");
        jni.admobShowPrivacyOptionsForm = methods.find("showPrivacyOptionsForm", "()V");
        jni.admobResetConsent = methods.find("resetConsent", "()V");
        jni.admobLoadAd = methods.find("loadAd", "(ILjava/lang/String;I)V");
        jni.admobShowAd = methods.find("showAd", "(I)V");
        jni.admobLoadBanner = methods.find("loadBanner", "(Ljava/lang/String;IIZI)V");
        jni.admobSetBannerVisible = methods.find("setBannerVisible", "(Z)V");
        jni.admobSetBannerPosition = methods.find("setBannerPosition", "(I)V");
        jni.admobRemoveBanner = methods.find("removeBanner", "()V");
        jni.admobSetServerSideVerificationOptions = methods.find("setServerSideVerificationOptions", "(Ljava/lang/String;Ljava/lang/String;)V");
        jni.admobSetAppVolume = methods.find("setAppVolume", "(F)V");
        jni.admobSetAppMuted = methods.find("setAppMuted", "(Z)V");
        jni.admobOpenAdInspector = methods.find("openAdInspector", "()V");

        if (methods.ok() && registerNatives(env, cls, admobNatives, sizeof(admobNatives) / sizeof(admobNatives[0]))){
            jni.adMobWrapperObjRef = env->NewGlobalRef(wrapper);
        }else{
            Log::error("The AdMob Java wrapper does not match the engine, AdMob is disabled");
        }

        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(wrapper);
    }

    if (jobject wrapper = findWrapper(env, jni.gameActivityObjRef, jni.gameActivityClsRef, "getBillingWrapper")){
        jclass cls = env->GetObjectClass(wrapper);
        MethodFinder methods(env, cls);

        jni.billingInitialize = methods.find("initialize", "()V");
        jni.billingIsReady = methods.find("isReady", "()Z");
        jni.billingQueryProducts = methods.find("queryProducts", "([Ljava/lang/String;I)V");
        jni.billingPurchase = methods.find("purchase", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;)V");
        jni.billingAcknowledgePurchase = methods.find("acknowledgePurchase", "(Ljava/lang/String;)V");
        jni.billingConsumePurchase = methods.find("consumePurchase", "(Ljava/lang/String;)V");
        jni.billingQueryPurchases = methods.find("queryPurchases", "(I)V");
        jni.billingOpenSubscriptionManagement = methods.find("openSubscriptionManagement", "(Ljava/lang/String;)V");
        jni.billingShowInAppMessages = methods.find("showInAppMessages", "()V");

        if (methods.ok() && registerNatives(env, cls, billingNatives, sizeof(billingNatives) / sizeof(billingNatives[0]))){
            jni.billingWrapperObjRef = env->NewGlobalRef(wrapper);
        }else{
            Log::error("The Google Play Billing Java wrapper does not match the engine, in-app purchases are disabled");
        }

        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(wrapper);
    }
}

void releaseServicesJNI(JNIEnv* env, JniData& jni){
    // the wrappers hold the activity
    if (jni.adMobWrapperObjRef){
        env->DeleteGlobalRef(jni.adMobWrapperObjRef);
        jni.adMobWrapperObjRef = nullptr;
    }
    if (jni.billingWrapperObjRef){
        env->DeleteGlobalRef(jni.billingWrapperObjRef);
        jni.billingWrapperObjRef = nullptr;
    }
}

bool DoriaxAndroid::admobInitialize(){
    JniData& jni = jniData();
    return callVoid(jni.adMobWrapperObjRef, jni.admobInitialize);
}

bool DoriaxAndroid::admobSetRequestConfiguration(AdMobRating rating, AdMobAgeRestriction ageRestriction, AdMobPersonalization personalization, const std::vector<std::string>& testDeviceIds){
    JniData& jni = jniData();
    JavaStringArray ids(jniEnv(), testDeviceIds);
    return callVoid(jni.adMobWrapperObjRef, jni.admobSetRequestConfiguration,
        static_cast<jint>(rating), static_cast<jint>(ageRestriction), static_cast<jint>(personalization), ids.get());
}

bool DoriaxAndroid::admobRequestConsent(bool underAgeOfConsent, AdMobDebugGeography debugGeography, const std::vector<std::string>& testDeviceIds){
    JniData& jni = jniData();
    JavaStringArray ids(jniEnv(), testDeviceIds);
    return callVoid(jni.adMobWrapperObjRef, jni.admobRequestConsent,
        static_cast<jboolean>(underAgeOfConsent), static_cast<jint>(debugGeography), ids.get());
}

AdMobConsentStatus DoriaxAndroid::admobGetConsentStatus(){
    JniData& jni = jniData();
    if (!jni.adMobWrapperObjRef) return AdMobConsentStatus::UNKNOWN;

    JNIEnv* env = jniEnv();
    jint status = env->CallIntMethod(jni.adMobWrapperObjRef, jni.admobGetConsentStatus);
    if (clearException(env)) return AdMobConsentStatus::UNKNOWN;
    return static_cast<AdMobConsentStatus>(status);
}

bool DoriaxAndroid::admobCanRequestAds(){
    JniData& jni = jniData();
    if (!jni.adMobWrapperObjRef) return false;

    JNIEnv* env = jniEnv();
    jboolean value = env->CallBooleanMethod(jni.adMobWrapperObjRef, jni.admobCanRequestAds);
    return !clearException(env) && value;
}

bool DoriaxAndroid::admobIsPrivacyOptionsRequired(){
    JniData& jni = jniData();
    if (!jni.adMobWrapperObjRef) return false;

    JNIEnv* env = jniEnv();
    jboolean value = env->CallBooleanMethod(jni.adMobWrapperObjRef, jni.admobIsPrivacyOptionsRequired);
    return !clearException(env) && value;
}

bool DoriaxAndroid::admobShowPrivacyOptionsForm(){
    JniData& jni = jniData();
    return callVoid(jni.adMobWrapperObjRef, jni.admobShowPrivacyOptionsForm);
}

void DoriaxAndroid::admobResetConsent(){
    JniData& jni = jniData();
    callVoid(jni.adMobWrapperObjRef, jni.admobResetConsent);
}

bool DoriaxAndroid::admobLoadAd(AdMobFormat format, const std::string& adUnitId, int generation){
    JniData& jni = jniData();
    JavaString id(jniEnv(), adUnitId);
    return callVoid(jni.adMobWrapperObjRef, jni.admobLoadAd, static_cast<jint>(format), id.get(), static_cast<jint>(generation));
}

bool DoriaxAndroid::admobShowAd(AdMobFormat format){
    JniData& jni = jniData();
    return callVoid(jni.adMobWrapperObjRef, jni.admobShowAd, static_cast<jint>(format));
}

bool DoriaxAndroid::admobLoadBanner(const std::string& adUnitId, AdMobBannerSize size, AdMobBannerPosition position, bool visible, int generation){
    JniData& jni = jniData();
    JavaString id(jniEnv(), adUnitId);
    return callVoid(jni.adMobWrapperObjRef, jni.admobLoadBanner, id.get(),
        static_cast<jint>(size), static_cast<jint>(position), static_cast<jboolean>(visible), static_cast<jint>(generation));
}

void DoriaxAndroid::admobSetBannerVisible(bool visible){
    JniData& jni = jniData();
    callVoid(jni.adMobWrapperObjRef, jni.admobSetBannerVisible, static_cast<jboolean>(visible));
}

void DoriaxAndroid::admobSetBannerPosition(AdMobBannerPosition position){
    JniData& jni = jniData();
    callVoid(jni.adMobWrapperObjRef, jni.admobSetBannerPosition, static_cast<jint>(position));
}

void DoriaxAndroid::admobRemoveBanner(){
    JniData& jni = jniData();
    callVoid(jni.adMobWrapperObjRef, jni.admobRemoveBanner);
}

void DoriaxAndroid::admobSetServerSideVerificationOptions(const std::string& userId, const std::string& customData){
    JniData& jni = jniData();
    JavaString user(jniEnv(), userId);
    JavaString data(jniEnv(), customData);
    callVoid(jni.adMobWrapperObjRef, jni.admobSetServerSideVerificationOptions, user.get(), data.get());
}

void DoriaxAndroid::admobSetAppVolume(float volume){
    JniData& jni = jniData();
    callVoid(jni.adMobWrapperObjRef, jni.admobSetAppVolume, static_cast<jfloat>(volume));
}

void DoriaxAndroid::admobSetAppMuted(bool muted){
    JniData& jni = jniData();
    callVoid(jni.adMobWrapperObjRef, jni.admobSetAppMuted, static_cast<jboolean>(muted));
}

bool DoriaxAndroid::admobOpenAdInspector(){
    JniData& jni = jniData();
    return callVoid(jni.adMobWrapperObjRef, jni.admobOpenAdInspector);
}

bool DoriaxAndroid::billingInitialize(){
    JniData& jni = jniData();
    return callVoid(jni.billingWrapperObjRef, jni.billingInitialize);
}

bool DoriaxAndroid::billingIsReady(){
    JniData& jni = jniData();
    if (!jni.billingWrapperObjRef) return false;

    JNIEnv* env = jniEnv();
    jboolean value = env->CallBooleanMethod(jni.billingWrapperObjRef, jni.billingIsReady);
    return !clearException(env) && value;
}

bool DoriaxAndroid::billingQueryProducts(const std::vector<std::string>& productIds, ProductType type){
    JniData& jni = jniData();
    JavaStringArray ids(jniEnv(), productIds);
    return callVoid(jni.billingWrapperObjRef, jni.billingQueryProducts, ids.get(), static_cast<jint>(type));
}

bool DoriaxAndroid::billingPurchase(const PurchaseParams& params){
    JniData& jni = jniData();
    JNIEnv* env = jniEnv();
    JavaString productId(env, params.productId);
    JavaString offerToken(env, params.offerToken);
    JavaString oldPurchaseToken(env, params.oldPurchaseToken);
    JavaString oldProductId(env, params.oldProductId);
    JavaString accountId(env, params.obfuscatedAccountId);
    JavaString profileId(env, params.obfuscatedProfileId);
    return callVoid(jni.billingWrapperObjRef, jni.billingPurchase, productId.get(), offerToken.get(),
        oldPurchaseToken.get(), oldProductId.get(), static_cast<jint>(params.replacementMode), accountId.get(), profileId.get());
}

bool DoriaxAndroid::billingAcknowledgePurchase(const std::string& purchaseToken){
    JniData& jni = jniData();
    JavaString token(jniEnv(), purchaseToken);
    return callVoid(jni.billingWrapperObjRef, jni.billingAcknowledgePurchase, token.get());
}

bool DoriaxAndroid::billingConsumePurchase(const std::string& purchaseToken){
    JniData& jni = jniData();
    JavaString token(jniEnv(), purchaseToken);
    return callVoid(jni.billingWrapperObjRef, jni.billingConsumePurchase, token.get());
}

bool DoriaxAndroid::billingQueryPurchases(ProductType type){
    JniData& jni = jniData();
    return callVoid(jni.billingWrapperObjRef, jni.billingQueryPurchases, static_cast<jint>(type));
}

bool DoriaxAndroid::billingOpenSubscriptionManagement(const std::string& productId){
    JniData& jni = jniData();
    JavaString id(jniEnv(), productId);
    return callVoid(jni.billingWrapperObjRef, jni.billingOpenSubscriptionManagement, id.get());
}

bool DoriaxAndroid::billingShowInAppMessages(){
    JniData& jni = jniData();
    return callVoid(jni.billingWrapperObjRef, jni.billingShowInAppMessages);
}
