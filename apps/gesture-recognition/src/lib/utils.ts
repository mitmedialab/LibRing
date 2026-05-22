export const debounce = (sec: number) => {
    let lastExec = -1;
    return (func: () => void) => {
        const curTime = Date.now();
        if (curTime - lastExec > sec * 1000) {
            func();
            lastExec = curTime;
        }
    };
};
