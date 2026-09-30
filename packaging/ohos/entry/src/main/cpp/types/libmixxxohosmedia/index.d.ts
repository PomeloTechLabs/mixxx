export const readState: () => string;
export const readArtwork: (key: string) => ArrayBuffer;
export const readWindowState: () => string;
export const sendCommand: (command: string, value: number) => boolean;
export const setKeyboardHeight: (height: number) => void;
export const setSafeArea: (left: number, top: number, right: number, bottom: number) => void;

declare const mediaBridge: {
  readState: typeof readState;
  readArtwork: typeof readArtwork;
  readWindowState: typeof readWindowState;
  sendCommand: typeof sendCommand;
  setKeyboardHeight: typeof setKeyboardHeight;
  setSafeArea: typeof setSafeArea;
};
export default mediaBridge;
