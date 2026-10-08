// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

// DoriaxWeb's WebPortal backends. Module.webPortal resolves to {environment, error, adapter},
// where each portal's adapter turns the engine calls into its SDK.

#include "DoriaxWeb.h"

#include <emscripten/emscripten.h>
#include <string.h>

#include "Engine.h"
#include "service/WebPortal.h"

using namespace doriax;

namespace {
    // Portals ask to pause and mute the game while an ad plays
    bool pausedForAd = false;

    void resumeAfterAd(){
        if (pausedForAd){
            pausedForAd = false;
            Engine::systemResume();
        }
    }

    // The adapter calls shared by the portals; they wait for the SDK and do nothing without it
    class ScriptPortal: public WebPortalBackend{
    public:
        virtual void requestAd(WebPortalAdType type) override{
            EM_ASM({
                var type = $0;
                var started = false;
                var fail = function(code, message) {
                    ccall("webportal_ad_error_callback", null, ["number", "string", "string", "number"], [type, code, message, started ? 1 : 0]);
                };
                if (!Module.webPortal) {
                    fail("unavailable", "Call WebPortal.initialize first");
                    return;
                }
                Module.webPortal.then(function(portal) {
                    if (!portal.adapter) {
                        fail("unavailable", "The portal SDK is not available here");
                        return;
                    }
                    portal.adapter.requestAd(type === 1, {
                        started: function() {
                            started = true;
                            ccall("webportal_ad_started_callback", null, ["number"], [type]);
                        },
                        finished: function() {
                            ccall("webportal_ad_finished_callback", null, ["number"], [type]);
                        },
                        failed: fail
                    });
                });
            }, static_cast<int>(type));
        }

        virtual void gameplayStart() override{
            callAdapter("gameplayStart");
        }

        virtual void gameplayStop() override{
            callAdapter("gameplayStop");
        }

        virtual void loadingStart() override{
            callAdapter("loadingStart");
        }

        virtual void loadingStop() override{
            callAdapter("loadingStop");
        }

        virtual void happytime() override{
            callAdapter("happytime");
        }

    private:
        void callAdapter(const char* method){
            EM_ASM({
                var name = UTF8ToString($0);
                if (Module.webPortal) {
                    Module.webPortal.then(function(portal) {
                        if (portal.adapter) portal.adapter[name]();
                    });
                }
            }, method);
        }
    };

    // HTML5 SDK v3. Its init resolves on any domain, with a disabled SDK that throws outside
    // CrazyGames and localhost.
    class CrazyGamesPortal: public ScriptPortal{
    public:
        virtual WebPortalType getType() override{
            return WebPortalType::CRAZYGAMES;
        }

        virtual void initialize() override{
            EM_ASM({
                if (!Module.webPortal) {
                    Module.webPortal = new Promise(function(resolve) {
                        var script = document.createElement("script");
                        script.src = "https://sdk.crazygames.com/crazygames-sdk-v3.js";
                        script.onload = function() {
                            var sdk = window.CrazyGames.SDK;
                            sdk.init().then(function() {
                                var environment = (sdk.environment === "local") ? "local" : ((sdk.environment === "crazygames") ? "portal" : "disabled");
                                resolve({environment: environment, error: "", adapter: (environment === "disabled") ? null : {
                                    requestAd: function(rewarded, events) {
                                        sdk.ad.requestAd(rewarded ? "rewarded" : "midgame", {
                                            adStarted: events.started,
                                            adFinished: events.finished,
                                            adError: function(error) {
                                                events.failed((error && error.code) ? error.code : "other", (error && error.message) ? error.message : "");
                                            }
                                        });
                                    },
                                    gameplayStart: function() { sdk.game.gameplayStart(); },
                                    gameplayStop: function() { sdk.game.gameplayStop(); },
                                    loadingStart: function() { sdk.game.loadingStart(); },
                                    loadingStop: function() { sdk.game.loadingStop(); },
                                    happytime: function() { sdk.game.happytime(); }
                                }});
                            }).catch(function(error) {
                                resolve({environment: "disabled", error: String((error && error.message) ? error.message : error), adapter: null});
                            });
                        };
                        script.onerror = function() {
                            resolve({environment: "disabled", error: "Could not load " + script.src, adapter: null});
                        };
                        document.head.appendChild(script);
                    });
                }
                Module.webPortal.then(function(portal) {
                    ccall("webportal_initialized_callback", null, ["string", "string"], [portal.environment, portal.error]);
                });
            });
        }
    };

    // Poki SDK v2, whose core patches the loader's window.PokiSDK once loaded. A failed init
    // still leaves a working SDK, and debug mode replaces ads on localhost.
    class PokiPortal: public ScriptPortal{
    public:
        virtual WebPortalType getType() override{
            return WebPortalType::POKI;
        }

        virtual void initialize() override{
            EM_ASM({
                if (!Module.webPortal) {
                    Module.webPortal = new Promise(function(resolve) {
                        var script = document.createElement("script");
                        script.src = "https://game-cdn.poki.com/scripts/v2/poki-sdk.js";
                        script.onload = function() {
                            var sdk = window.PokiSDK;
                            var host = window.location.hostname;
                            var environment = (host === "localhost" || host === "127.0.0.1" || host === "[::1]") ? "local" : "portal";
                            var adapter = {
                                requestAd: function(rewarded, events) {
                                    var started = false;
                                    var onStart = function() {
                                        started = true;
                                        events.started();
                                    };
                                    if (rewarded) {
                                        sdk.rewardedBreak(onStart).then(function(success) {
                                            if (success) events.finished();
                                            else events.failed(started ? "other" : "unfilled", started ? "The ad did not grant a reward" : "No ad played");
                                        });
                                    } else {
                                        sdk.commercialBreak(onStart).then(function() {
                                            if (started) events.finished();
                                            else events.failed("unfilled", "No ad played at this break");
                                        });
                                    }
                                },
                                gameplayStart: function() { sdk.gameplayStart(); },
                                gameplayStop: function() { sdk.gameplayStop(); },
                                loadingStart: function() { sdk.gameLoadingStart(); },
                                loadingStop: function() { sdk.gameLoadingFinished(); },
                                happytime: function() { sdk.happyTime(); }
                            };
                            sdk.init().then(function() {
                                resolve({environment: environment, error: "", adapter: adapter});
                            }).catch(function() {
                                resolve({environment: environment, error: "Poki SDK init failed, ads may not play", adapter: adapter});
                            });
                        };
                        script.onerror = function() {
                            resolve({environment: "disabled", error: "Could not load " + script.src, adapter: null});
                        };
                        document.head.appendChild(script);
                    });
                }
                Module.webPortal.then(function(portal) {
                    ccall("webportal_initialized_callback", null, ["string", "string"], [portal.environment, portal.error]);
                });
            });
        }
    };
}

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void webportal_initialized_callback(const char* environment, const char* error) {
        WebPortalEnvironment value = WebPortalEnvironment::DISABLED;
        if (strcmp(environment, "local") == 0){
            value = WebPortalEnvironment::LOCAL;
        }else if (strcmp(environment, "portal") == 0){
            value = WebPortalEnvironment::PORTAL;
        }
        WebPortal::systemInitialized(value, error);
    }

    EMSCRIPTEN_KEEPALIVE
    void webportal_ad_started_callback(int type) {
        if (!pausedForAd){
            pausedForAd = true;
            Engine::systemPause();
        }
        WebPortal::systemAdStarted(static_cast<WebPortalAdType>(type));
    }

    EMSCRIPTEN_KEEPALIVE
    void webportal_ad_finished_callback(int type) {
        resumeAfterAd();
        WebPortal::systemAdFinished(static_cast<WebPortalAdType>(type));
    }

    // a request refused while another ad plays must not resume the game
    EMSCRIPTEN_KEEPALIVE
    void webportal_ad_error_callback(int type, const char* code, const char* message, int started) {
        if (started){
            resumeAfterAd();
        }
        WebPortal::systemAdError(static_cast<WebPortalAdType>(type), code, message);
    }
}

WebPortalBackend* DoriaxWeb::getWebPortalBackend(){
#if defined(DORIAX_WEB_PORTAL_CRAZYGAMES)
    static CrazyGamesPortal portal;
    return &portal;
#elif defined(DORIAX_WEB_PORTAL_POKI)
    static PokiPortal portal;
    return &portal;
#else
    return nullptr;
#endif
}
