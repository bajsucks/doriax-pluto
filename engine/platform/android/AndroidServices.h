// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef AndroidServices_H_
#define AndroidServices_H_

#include <jni.h>

struct JniData;

// Finds the AdMob and Google Play Billing wrappers the export compiled in and registers their natives
void setupServicesJNI(JNIEnv* env, JniData& jniData);
void releaseServicesJNI(JNIEnv* env, JniData& jniData);

#endif /* AndroidServices_H_ */
