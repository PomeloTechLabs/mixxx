import hilog from '@ohos.hilog';

interface WindowNativeNodeCreateInfo {
  xComponentId: string
  onAppear: () => void
  onAttach: () => void
  onDisAppear: () => void
}

@Entry
@Component
struct WindowNativeNode {
  @State private createInfo: WindowNativeNodeCreateInfo | undefined = undefined

  aboutToAppear(): void {
    const localStorage  = this.getUIContext().getSharedLocalStorage();
    if (localStorage ) {
      this.createInfo = localStorage.get<WindowNativeNodeCreateInfo>('createInfo');
    }
  }

  build() {
    Stack() {
      if (this.createInfo !== undefined) {
        XComponent({
          type: XComponentType.NODE,
          id: this.createInfo.xComponentId,
          libraryname: 'qohos'
        })
        .onAttach(() => {
          hilog.info(0x0000, 'mixxxohos', 'XComponent onAttach event');
          this.createInfo?.onAttach();
        })
        .onAppear(() => {
          hilog.info(0x0000, 'mixxxohos', 'XComponent onAppear event');
          this.createInfo?.onAppear();
        })
        .onDisAppear(() => {
          hilog.info(0x0000, 'mixxxohos', 'XComponent onDisAppear event');
          this.createInfo?.onDisAppear();
        })
        .width('100%')
        .height('100%')
      }
    }
    .width('100%')
    .height('100%')
  }
}
