/**
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <jni.h>
#include <string>
#include "native_debug.h"
#include "camera/camera_manager.h"
#include "camera/camera_engine.h"
#include <android/asset_manager_jni.h>


#define PROTOBUF_USE_DLLS 1
#define CAFFE2_USE_LITE_PROTO 1


CameraAppEngine *pEngineObj = nullptr;
jboolean        _caffe_is_running = 0;
static caffe2::NetDef _initNet, _predictNet;
static caffe2::Predictor *_predictor;

// A function to load the NetDefs from protobufs.
void loadToNetDef(AAssetManager* mgr, caffe2::NetDef* net, const char *filename) {
    AAsset* asset = AAssetManager_open(mgr, filename, AASSET_MODE_BUFFER);
    assert(asset != nullptr);
    const void *data = AAsset_getBuffer(asset);
    assert(data != nullptr);
    off_t len = AAsset_getLength(asset);
    assert(len != 0);
    if (!net->ParseFromArray(data, len)) {
        LOGI("Couldn't parse net from data.\n");
    }
    AAsset_close(asset);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sample_textureview_ViewActivity_initCaffe2( JNIEnv* env,
                                                     jobject thiz ,
                                                     jobject assetManager) {
    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);
    LOGI("Attempting to load protobuf netdefs...");
    loadToNetDef(mgr, &_initNet,   "squeeze_init_net.pb");
    loadToNetDef(mgr, &_predictNet,"squeeze_predict_net.pb");
    _predictor = new caffe2::Predictor(_initNet, _predictNet);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sample_textureview_ViewActivity_IsCaffeRunning( JNIEnv* env,
                                                         jobject thiz,
                                                         jlong ndkCameraObj)
{
    ASSERT(ndkCameraObj, "NativeObject should not be null Pointer");
    //CameraAppEngine *pApp = reinterpret_cast<CameraAppEngine *>(ndkCameraObj);
    return _caffe_is_running;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sample_textureview_ViewActivity_RunCaffe( JNIEnv* env,
                                                   jobject thiz,
                                                   jlong ndkCameraObj )
{
    ASSERT(ndkCameraObj, "NativeObject should not be null Pointer");
    CameraAppEngine *pApp = reinterpret_cast<CameraAppEngine *>(ndkCameraObj);

    _caffe_is_running = 1;

    pApp->GetJPGImageReader()->_caffe_result_is_ready = false;
    pApp->GetJPGImageReader()->SetCaffePredictor(_predictor);

    std::thread takePhotoHandler(&CameraAppEngine::TakePhoto, pEngineObj);
    takePhotoHandler.detach();

    while(pApp->GetJPGImageReader()->_caffe_result_is_ready == false);

    _caffe_is_running = 0;
    return env->NewStringUTF((pApp->GetJPGImageReader()->_CaffeResult).c_str());
}

/**
 * createCamera() Create application instance and NDK camera object
 * @param  width is the texture view window width
 * @param  height is the texture view window height
 * In this sample, it takes the simplest approach in that:
 * the android system display size is used to view full screen
 * preview camera images. Onboard Camera most likely to
 * support the onboard panel size on that device. Camera is most likely
 * to be oriented as landscape mode, the input width/height is in
 * landscape mode. TextureView on Java side handles rotation for
 * portrait mode.
 * @return application object instance ( not used in this sample )
 */

extern "C" JNIEXPORT jlong JNICALL
Java_com_sample_textureview_ViewActivity_createCamera(JNIEnv *env,
                                                      jobject instance,
                                                      jint width, jint height)
{
    pEngineObj = new CameraAppEngine(env, instance, width, height);
    return reinterpret_cast<jlong>(pEngineObj);
}

/**
 * deleteCamera():
 *   releases native application object, which
 *   triggers native camera object be released
 */
extern "C" JNIEXPORT void JNICALL
Java_com_sample_textureview_ViewActivity_deleteCamera(JNIEnv *env,
                                                      jobject instance,
                                                      jlong ndkCameraObj) {
    if (!pEngineObj || !ndkCameraObj) {
        return;
    }
    CameraAppEngine *pApp = reinterpret_cast<CameraAppEngine *>(ndkCameraObj);
    ASSERT(pApp == pEngineObj, "NdkCamera Obj mismatch");

    delete pApp;

    // also reset the private global object
    pEngineObj = nullptr;
}

/**
 * getCameraCompatibleSize()
 * @returns minimium camera preview window size for the given
 * requested camera size in CreateCamera() function, with the same
 * ascpect ratio. essentially,
 *   1) Display device size decides NDKCamera object preview size
 *   2) Chosen NDKCamera preview size passed to TextView to
 *      reset textureView size
 *   3) textureView size is stretched when previewing image
 *      on display device
 */
extern "C" JNIEXPORT jobject JNICALL
Java_com_sample_textureview_ViewActivity_getMinimumCompatiblePreviewSize( JNIEnv *env,
                                                                          jobject instance,
                                                                          jlong ndkCameraObj)
{
    if (!ndkCameraObj) {
        return nullptr;
    }
    CameraAppEngine *pApp = reinterpret_cast<CameraAppEngine *>(ndkCameraObj);
    jclass cls = env->FindClass("android/util/Size");
    jobject previewSize =
            env->NewObject(cls, env->GetMethodID(cls, "<init>", "(II)V"),
                           pApp->GetCompatibleCameraRes().width,
                           pApp->GetCompatibleCameraRes().height);
    return previewSize;
}

/**
 * getCameraSensorOrientation()
 * @ return camera sensor orientation angle relative to Android device's
 * display orientation. This sample only deal to back facing camera.
 */
extern "C" JNIEXPORT jint JNICALL
Java_com_sample_textureview_ViewActivity_getCameraSensorOrientation( JNIEnv *env,
                                                                     jobject instance,
                                                                     jlong ndkCameraObj )
{
    ASSERT(ndkCameraObj, "NativeObject should not be null Pointer");
    CameraAppEngine *pApp = reinterpret_cast<CameraAppEngine *>(ndkCameraObj);
    return pApp->GetCameraSensorOrientation(ACAMERA_LENS_FACING_BACK);
}

/**
 * OnPreviewSurfaceCreated()
 *   Notification to native camera that java TextureView is ready
 *   to preview video. Simply create cameraSession and
 *   start camera preview
 */


extern "C" JNIEXPORT void JNICALL
Java_com_sample_textureview_ViewActivity_onPreviewSurfaceCreated( JNIEnv   *env,
                                                                  jobject  thiz,
                                                                  jlong    ndkCameraObj,
                                                                  jobject  surface,
                                                                  jstring  dir)
{
    ASSERT(ndkCameraObj && (jlong) pEngineObj == ndkCameraObj,
           "NativeObject should not be null Pointer");

    CameraAppEngine *pApp = reinterpret_cast<CameraAppEngine *>(ndkCameraObj);
    pApp->CreateJPGImageReader();
    pApp->CreateCameraSession(surface);
    pApp->StartPreview(true);
}

/**
 * OnPreviewSurfaceDestroyed()
 *   Notification to native camera that java TextureView is destroyed
 *   Native camera would:
 *      * stop preview
 */
extern "C" JNIEXPORT void JNICALL
Java_com_sample_textureview_ViewActivity_onPreviewSurfaceDestroyed( JNIEnv *env,
                                                                    jobject instance,
                                                                    jlong ndkCameraObj,
                                                                    jobject surface )
{
    CameraAppEngine *pApp = reinterpret_cast<CameraAppEngine *>(ndkCameraObj);
    ASSERT(ndkCameraObj && pEngineObj == pApp,
           "NativeObject should not be null Pointer");
    jclass cls = env->FindClass("android/view/Surface");
    jmethodID toString =
            env->GetMethodID(cls, "toString", "()Ljava/lang/String;");

    jstring destroyObjStr =
            reinterpret_cast<jstring>(env->CallObjectMethod(surface, toString));
    const char *destroyObjName = env->GetStringUTFChars(destroyObjStr, nullptr);

    jstring appObjStr = reinterpret_cast<jstring>(
            env->CallObjectMethod(pApp->GetSurfaceObject(), toString));
    const char *appObjName = env->GetStringUTFChars(appObjStr, nullptr);

    ASSERT(!strcmp(destroyObjName, appObjName), "object Name MisMatch");

    env->ReleaseStringUTFChars(destroyObjStr, destroyObjName);
    env->ReleaseStringUTFChars(appObjStr, appObjName);

    pApp->StartPreview(false);
}
