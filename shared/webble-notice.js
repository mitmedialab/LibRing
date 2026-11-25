// Shows a banner at the top of the page when the browser has no Web Bluetooth
// (iPhone / iPad: Safari and Chrome both lack it, Bluefy has it; desktop: Safari,
// Firefox). Plain script with no exports: include it with
//   <script src="../shared/webble-notice.js"></script>
// or, in the Vite apps, import '../../shared/webble-notice.js'.
(function () {
    if (typeof navigator === 'undefined' || navigator.bluetooth) return;

    var BLUEFY = 'https://apps.apple.com/us/app/bluefy-web-ble-browser/id1492822055';
    var ua = navigator.userAgent || '';
    // iPadOS reports itself as a Mac: tell them apart by the touch screen
    var ios = /iPhone|iPad|iPod/.test(ua) ||
        (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1);
    var android = /Android/.test(ua);

    var html;
    if (ios) {
        html = '<b>This browser can\'t connect to the ring.</b> On iPhone and iPad, Safari and ' +
            'Chrome have no Web Bluetooth: open this page in a Web Bluetooth browser such as ' +
            '<a href="' + BLUEFY + '" target="_blank" rel="noopener">Bluefy</a> (free on the App Store).';
    } else if (android) {
        html = '<b>This browser can\'t connect to the ring</b> (no Web Bluetooth): ' +
            'open this page in Chrome.';
    } else {
        html = '<b>This browser can\'t connect to the ring</b> (no Web Bluetooth): ' +
            'open this page in Chrome or Edge.';
    }

    function show() {
        if (document.getElementById('webble-notice')) return;
        var bar = document.createElement('div');
        bar.id = 'webble-notice';
        bar.setAttribute('role', 'alert');
        bar.style.cssText =
            'position:fixed;top:0;left:0;right:0;z-index:2147483647;box-sizing:border-box;' +
            'padding:12px 44px 12px 16px;background:#fff3cd;color:#3d2e00;' +
            'border-bottom:2px solid #e0a800;box-shadow:0 2px 8px rgba(0,0,0,.25);' +
            'font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;text-align:left;';
        bar.innerHTML = html;
        var link = bar.querySelector('a');
        if (link) link.style.cssText = 'color:#0b5394;font-weight:600;text-decoration:underline;';

        var close = document.createElement('button');
        close.type = 'button';
        close.setAttribute('aria-label', 'Close');
        close.textContent = '×';
        close.style.cssText =
            'position:absolute;top:6px;right:8px;width:32px;height:32px;padding:0;border:0;' +
            'background:none;color:inherit;font-size:24px;line-height:32px;cursor:pointer;';
        close.onclick = function () { bar.remove(); };
        bar.appendChild(close);
        document.body.appendChild(bar);
    }

    if (document.body) show();
    else document.addEventListener('DOMContentLoaded', show);
})();
