/*
 * HarmonyOS ArkTS 封装：USB Transport + 相机控制器。
 * 通过 NAPI 模块 "camcore"（harmony/napi/camcore_napi.cpp）调用共享 CamCore。
 *
 * 构建：DevEco Studio，oh-package + hvigor，见 harmony/README.md。
 * USB API：@ohos.usbManager（bulkTransfer 单次 < 200KB，需 192KB 分块，见 docs/PROTOCOL.md）。
 */

import usbManager from '@ohos.usbManager';
import camcore from 'libcamcore.so';

/** NAPI 模块（TypeScript 声明，对应 camcore_napi.cpp 导出）。 */
declare namespace camcore {
  function setTransport(transport: NativeTransport, listener: NativeEventListener): void;
  function createSession(): number;
  function connect(): number;
  function capture(): number;
  function download(handle: number, path: string): number;
  function setParam(prop: number, value: number): number;
  function destroy(): void;
}

export interface NativeTransport {
  nativeConnect(): number;
  nativeDisconnect(): number;
  nativeSend(data: ArrayBuffer): number;
  nativeRecv(buf: ArrayBuffer): number;
  nativeSetTimeout(timeoutMs: number): number;
}

export interface NativeEventListener {
  onNativeEvent(type: number, arg0: number, arg1: number): void;
}

const NIKON_VID = 0x04B0;
const STILL_IMAGE_CLASS = 6;

/** USB-PTP Transport（ArkTS 实现）。 */
export class UsbTransport implements NativeTransport {
  private pipe: usbManager.USBDevicePipe | null = null;
  private interface_: usbManager.USBInterface | null = null;
  private epIn: usbManager.USBEndpoint | null = null;
  private epOut: usbManager.USBEndpoint | null = null;

  static open(device: usbManager.USBDevice): UsbTransport | null {
    if (device.devVendorId !== NIKON_VID) return null;
    const t = new UsbTransport();
    for (const itf of device.configs[0].interfaces) {
      if (itf.clazz === STILL_IMAGE_CLASS) {
        t.interface_ = itf;
        for (const ep of itf.endpoints) {
          if (ep.direction === 0x80) t.epIn = ep;   // IN
          else if (ep.direction === 0x00) t.epOut = ep; // OUT
        }
        break;
      }
    }
    if (!t.epIn || !t.epOut) return null;
    return t;
  }

  nativeConnect(): number {
    return 0;
  }

  nativeDisconnect(): number {
    if (this.pipe && this.interface_) {
      usbManager.releaseInterface(this.pipe, this.interface_);
      usbManager.closePipe(this.pipe);
    }
    return 0;
  }

  nativeSend(data: ArrayBuffer): number {
    if (!this.pipe || !this.epOut) return -1;
    return usbManager.bulkTransfer(this.pipe, this.epOut, new Uint8Array(data), 1000);
  }

  nativeRecv(buf: ArrayBuffer): number {
    if (!this.pipe || !this.epIn) return -1;
    return usbManager.bulkTransfer(this.pipe, this.epIn, new Uint8Array(buf), 1000);
  }

  nativeSetTimeout(timeoutMs: number): number {
    return 0;
  }
}

/** 高层控制器。 */
export class CameraController implements NativeEventListener {
  connectUsb(device: usbManager.USBDevice): boolean {
    const t = UsbTransport.open(device);
    if (!t) return false;
    camcore.setTransport(t, this);
    return camcore.createSession() === 0 && camcore.connect() === 0;
  }

  capture(): boolean {
    return camcore.capture() === 0;
  }

  download(handle: number, path: string): boolean {
    return camcore.download(handle, path) === 0;
  }

  setParam(prop: number, value: number): boolean {
    return camcore.setParam(prop, value) === 0;
  }

  onNativeEvent(type: number, arg0: number, arg1: number): void {
    // 事件回传：连接/拍摄完成/进度
  }
}
