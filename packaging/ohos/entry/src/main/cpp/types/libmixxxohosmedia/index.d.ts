export const readState: () => string;
export const readWindowState: () => string;
export const sendCommand: (command: string, value: number) => boolean;
export const setKeyboardHeight: (height: number) => void;

declare const mediaBridge: {
  readState: typeof readState;
  readWindowState: typeof readWindowState;
  sendCommand: typeof sendCommand;
  setKeyboardHeight: typeof setKeyboardHeight;
};
export default mediaBridge;
