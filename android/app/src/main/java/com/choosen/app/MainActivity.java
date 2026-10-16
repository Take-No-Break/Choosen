package com.choosen.app;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.ImageDecoder;
import android.graphics.Matrix;
import android.media.ExifInterface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.SystemClock;
import android.provider.MediaStore;
import android.provider.DocumentsContract;
import android.util.LruCache;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Semaphore;

/**
 * Choosen on Android: the same pages as on Windows (web/ and Choosen V3 from web3/dist, packed as
 * assets) in a WebView, and the same C++ core (Core). There is no HTTP server: every request of the
 * page is answered inside the app, and nothing is sent anywhere. The app opens Choosen V3 unless the
 * owner switched back to the normal page (kept in the "ui" preference).
 */
public class MainActivity extends Activity {
  /** Never resolved: every request to it is answered by shouldInterceptRequest. */
  static final String HOST = "choosen.app";
  private static final int REQ_PERMISSION = 1, REQ_PICK = 2;
  /** Formats the WebView decodes itself; anything else (HEIC, TIFF, ...) is converted to JPEG. */
  private static final String[] BROWSER_FORMATS = {"jpg", "jpeg", "jfif", "png", "gif", "webp", "avif", "bmp"};

  private WebView web;
  private final ExecutorService background = Executors.newSingleThreadExecutor();
  private final LruCache<String, byte[]> thumbs = new LruCache<String, byte[]>(48 * 1024 * 1024) {
    @Override
    protected int sizeOf(String key, byte[] value) {
      return value.length;
    }
  };
  /** At most two screen-fit decodes at once: each needs a full-size bitmap for a moment. */
  private final Semaphore fitDecodes = new Semaphore(2);
  /** The camera's notch at the top, in px; Choosen V3 draws under it and keeps its buttons below it. */
  private int safeTop;
  /** Choosen V3 is shown (it handles the top itself); the normal page gets the notch as padding. */
  private volatile boolean v3Page = true;
  /** The PC whose library is shown ("192.168.1.20:47801"), "" when the phone's own library is. */
  private volatile String pcHost = "";
  /** The page on screen is the app's own (only it may use the bridge to the phone's library). */
  private volatile boolean ownPage = true;
  private android.widget.FrameLayout frame;
  private volatile CountDownLatch pickDone;
  private volatile String picked = "";
  /** When the app went behind another one, for the launch screen when it comes back. */
  private long awaySince;
  /** The system's folder picker is (or was just) open: coming back from it is not a new start. */
  private volatile boolean picking;

  @Override
  protected void onCreate(Bundle state) {
    super.onCreate(state);
    preferFastestRefresh(); // 120 Hz where the screen has it
    // Debug builds only: lets the developer inspect the page from the PC (chrome://inspect).
    if ((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0) WebView.setWebContentsDebuggingEnabled(true);
    web = new WebView(this);
    web.setBackgroundColor(0xFFFFFFFF);  // white like the launch screen until the page paints
    // Full screen like a video app: the status bar (clock, battery) and the navigation bar are hidden
    // and come back for a moment with a swipe from the top or bottom edge. The page reaches the top
    // of the screen; the keyboard (and a navigation bar that is shown) stays outside it as padding.
    frame = new android.widget.FrameLayout(this);
    frame.setBackgroundColor(0xFF0E0F11);
    frame.addView(web);
    frame.setOnApplyWindowInsetsListener((v, insets) -> {
      int l, t, r, b, notch;
      if (Build.VERSION.SDK_INT >= 30) {
        android.graphics.Insets bars = insets.getInsets(android.view.WindowInsets.Type.navigationBars()
            | android.view.WindowInsets.Type.displayCutout() | android.view.WindowInsets.Type.ime());
        notch = Math.max(insets.getInsets(android.view.WindowInsets.Type.displayCutout()).top,
            insets.getInsets(android.view.WindowInsets.Type.statusBars()).top);
        l = bars.left; r = bars.right; b = bars.bottom;
      } else {
        l = insets.getSystemWindowInsetLeft(); r = insets.getSystemWindowInsetRight();
        b = insets.getSystemWindowInsetBottom(); notch = insets.getSystemWindowInsetTop();
      }
      safeTop = notch;
      t = v3Page ? 0 : notch;
      v.setPadding(l, t, r, b);
      sendSafeTop();
      return insets;
    });
    setContentView(frame);
    if (Build.VERSION.SDK_INT >= 30) getWindow().getAttributes().layoutInDisplayCutoutMode =
        android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
    hideBars();
    // The back button: one press goes back in the page (the previous picture); two quick presses
    // leave for the home screen. Android 13+ delivers it here (onBackPressed is not called any more
    // for apps that target Android 16).
    if (Build.VERSION.SDK_INT >= 33)
      getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
          android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::systemBack);
    WebSettings s = web.getSettings();
    s.setJavaScriptEnabled(true);
    s.setDomStorageEnabled(true);  // the page keeps its preferences in localStorage
    // the phone's font size: followed, but at most 115 % so the screens keep their layout on any phone
    s.setTextZoom(Math.min(115, Math.round(getResources().getConfiguration().fontScale * 100)));
    web.addJavascriptInterface(new Bridge(), "ChoosenAndroid");
    web.setWebViewClient(new Client());
    final String dataDir = new File(getFilesDir(), "data").getAbsolutePath();
    new File(dataDir).mkdirs();
    background.execute(() -> {
      Core.init(dataDir);
      runOnUiThread(() -> web.loadUrl("https://" + HOST + ("v1".equals(ui()) ? "/" : "/v3/")));
    });
    askForPhotos();
  }

  @Override
  protected void onPause() {
    super.onPause();
    background.execute(Core::saveIndex);
  }

  @Override
  protected void onResume() {
    super.onResume();
    hideBars();
    background.execute(Core::dailyBackup);
  }

  @Override
  protected void onStop() {
    super.onStop();
    awaySince = SystemClock.elapsedRealtime();
  }

  /** Back in front after a while (not from the folder picker): the page plays its launch screen. */
  @Override
  protected void onRestart() {
    super.onRestart();
    boolean fromPicker = picking;
    picking = false;
    if (!fromPicker && SystemClock.elapsedRealtime() - awaySince > 1000)
      web.evaluateJavascript("window.choosenHello && window.choosenHello()", null);
  }

  private void hideBars() {
    if (Build.VERSION.SDK_INT < 30) return;
    getWindow().setDecorFitsSystemWindows(false);
    android.view.WindowInsetsController c = getWindow().getInsetsController();
    if (c == null) return;
    c.hide(android.view.WindowInsets.Type.systemBars());
    c.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
  }

  private long lastBack;

  /** One press: back in the page; two presses within 0.4 s, or nothing to go back to: home screen. */
  private void systemBack() {
    long now = android.os.SystemClock.uptimeMillis();
    if (now - lastBack < 400) {
      lastBack = 0;
      moveTaskToBack(true);
      return;
    }
    lastBack = now;
    if (!v3Page) {  // the normal page: its own history
      if (web.canGoBack()) web.goBack();
      else moveTaskToBack(true);
      return;
    }
    web.evaluateJavascript("window.choosenBack ? window.choosenBack() : false", r -> {
      if (!"true".equals(r)) moveTaskToBack(true);
    });
  }

  /** Tells Choosen V3 how far down its buttons must stay (CSS --safe-top, in CSS pixels). */
  private void sendSafeTop() {
    if (web == null) return;
    float css = safeTop / getResources().getDisplayMetrics().density;
    web.evaluateJavascript("document.documentElement.style.setProperty('--safe-top','" + Math.round(css) + "px')", null);
  }

  @Override
  public void onBackPressed() {  // Android 12 and older
    systemBack();
  }

  private String ui() {
    return getSharedPreferences("choosen", MODE_PRIVATE).getString("ui", "v3");
  }

  // ---- photo access ------------------------------------------------------------------------

  private void askForPhotos() {
    String p = Build.VERSION.SDK_INT >= 33 ? Manifest.permission.READ_MEDIA_IMAGES : Manifest.permission.READ_EXTERNAL_STORAGE;
    if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) requestPermissions(new String[] {p}, REQ_PERMISSION);
  }

  @Override
  public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
    if (code == REQ_PERMISSION) background.execute(Core::rescan);  // the folders can be read now
  }

  /** Lets the owner choose a photo folder; blocks the calling (WebView IO) thread until done. */
  private String pickFolder() {
    pickDone = new CountDownLatch(1);
    picked = "";
    picking = true;
    runOnUiThread(() -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_PICK));
    try {
      pickDone.await();
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
    }
    return picked;
  }

  @Override
  protected void onActivityResult(int code, int result, Intent data) {
    super.onActivityResult(code, result, data);
    if (code != REQ_PICK) return;
    if (result == RESULT_OK && data != null && data.getData() != null) picked = treeToPath(data.getData());
    if (pickDone != null) pickDone.countDown();
  }

  /**
   * The picked folder as a file path: "primary:Pictures/Pixiv" -> /storage/emulated/0/Pictures/Pixiv,
   * "1234-ABCD:x" -> /storage/1234-ABCD/x, Downloads ("raw:/storage/...", "downloads") -> that path.
   * "" for places that are not folders on the phone's storage (cloud drives, "Recent").
   */
  static String treeToPath(Uri tree) {
    String id = DocumentsContract.getTreeDocumentId(tree);
    if (id.startsWith("raw:")) return id.substring(4);
    if ("com.android.providers.downloads.documents".equals(tree.getAuthority())) return "/storage/emulated/0/Download";
    int c = id.indexOf(':');
    if (c < 0) return "";
    String volume = id.substring(0, c), rest = id.substring(c + 1);
    if (!"primary".equals(volume) && !volume.matches("[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}")) return "";
    String base = "primary".equals(volume) ? "/storage/emulated/0" : "/storage/" + volume;
    return rest.isEmpty() ? base : base + "/" + rest;
  }

  /**
   * The app's own folder browser (Android's picker cannot choose the top of the storage or of
   * Download): the storage volumes for path "", otherwise the subfolders of path.
   */
  static String listDirs(String path) {
    StringBuilder o = new StringBuilder("{\"path\":").append(jsonString(path == null ? "" : path)).append(",\"dirs\":[");
    java.util.List<String[]> dirs = new java.util.ArrayList<>();
    if (path == null || path.isEmpty()) {
      dirs.add(new String[] {"内部ストレージ", "/storage/emulated/0"});
      File[] vols = new File("/storage").listFiles();
      if (vols != null)
        for (File v : vols)
          if (v.getName().matches("[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}")) dirs.add(new String[] {"SD カード (" + v.getName() + ")", v.getPath()});
    } else {
      File[] kids = new File(path).listFiles();
      if (kids != null) {
        java.util.Arrays.sort(kids, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
        for (File k : kids)
          if (k.isDirectory() && !k.getName().startsWith(".") && !(k.getName().equals("Android") && path.endsWith("/0")))
            dirs.add(new String[] {k.getName(), k.getPath()});
      }
    }
    for (int i = 0; i < dirs.size(); i++)
      o.append(i > 0 ? "," : "").append("{\"name\":").append(jsonString(dirs.get(i)[0])).append(",\"path\":").append(jsonString(dirs.get(i)[1])).append('}');
    return o.append("]}").toString();
  }

  // ---- the page's requests ---------------------------------------------------------------------

  /** POST requests with a body (shouldInterceptRequest cannot see bodies); called by app.js. */
  final class Bridge {
    @JavascriptInterface
    public String post(String url, String body) {
      if (!ownPage) return "403\n{}"; // a PC's page never reaches the phone's library
      int q = url.indexOf('?');
      return Core.api("POST", q < 0 ? url : url.substring(0, q), q < 0 ? "" : url.substring(q + 1), body == null ? "" : body);
    }

    /** The home screen icon's colour: "blue" or "purple" (the earlier icon). */
    @JavascriptInterface
    public String iconColor() {
      return getPackageManager().getComponentEnabledSetting(alias("LauncherPurple"))
              == android.content.pm.PackageManager.COMPONENT_ENABLED_STATE_ENABLED ? "purple" : "blue";
    }

    /**
     * Switches the home screen icon: the chosen launcher alias on, the other off (the app keeps
     * running). Some launchers take a moment, and may move the icon or need it placed again.
     */
    @JavascriptInterface
    public void setIconColor(String color) {
      if (!ownPage || !("blue".equals(color) || "purple".equals(color))) return;
      final android.content.pm.PackageManager pm = getPackageManager();
      final boolean purple = "purple".equals(color);
      pm.setComponentEnabledSetting(alias(purple ? "LauncherPurple" : "LauncherBlue"),
          android.content.pm.PackageManager.COMPONENT_ENABLED_STATE_ENABLED, android.content.pm.PackageManager.DONT_KILL_APP);
      pm.setComponentEnabledSetting(alias(purple ? "LauncherBlue" : "LauncherPurple"),
          android.content.pm.PackageManager.COMPONENT_ENABLED_STATE_DISABLED, android.content.pm.PackageManager.DONT_KILL_APP);
    }

    /** Which page the app opens from now on: "v3" (Choosen V3) or "v1" (the normal page). */
    @JavascriptInterface
    public void setUi(String ui) {
      if (!ownPage) return;
      if ("v1".equals(ui) || "v3".equals(ui)) getSharedPreferences("choosen", MODE_PRIVATE).edit().putString("ui", ui).apply();
    }

    /** Shows the library of the PC at addr ("192.168.1.20" or "192.168.1.20:47801"); code pairs it. */
    @JavascriptInterface
    public String openPc(String addr, String code) {
      if (!ownPage) return "no";
      String host = pcAddress(addr);
      if (host.isEmpty()) return "address";
      String pair = code != null && code.matches("\\d{6}") ? "#pair=" + code : "";
      getSharedPreferences("choosen", MODE_PRIVATE).edit().putString("pc", host).apply();
      pcHost = host;
      runOnUiThread(() -> web.loadUrl("http://" + host + "/v3/" + pair));
      return "ok";
    }

    /** Back to the phone's own library. */
    @JavascriptInterface
    public void leavePc() {
      pcHost = "";
      runOnUiThread(() -> web.loadUrl("https://" + HOST + "/v3/"));
    }

    /** The PC used last ("" if none), for the settings. */
    @JavascriptInterface
    public String lastPc() {
      return ownPage ? getSharedPreferences("choosen", MODE_PRIVATE).getString("pc", "") : "";
    }

    /** Whether the app may move the owner's pictures (all files access, given once in Android's settings). */
    @JavascriptInterface
    public boolean canMove() {
      return ownPage && (Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager());
    }

    /** Opens Android's page to allow all files access for Choosen (the owner switches it on once). */
    @JavascriptInterface
    public void askMove() {
      if (!ownPage || Build.VERSION.SDK_INT < 30) return;
      picking = true; // coming back from Android's page is not a new start (no launch screen)
      runOnUiThread(() -> {
        try {
          startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
              Uri.parse("package:" + getPackageName())));
        } catch (RuntimeException e) {
          startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
      });
    }

    /** Tells the phone's gallery about moved files (their old and new paths, one per line). */
    @JavascriptInterface
    public void mediaScan(String paths) {
      if (!ownPage || paths == null || paths.isEmpty()) return;
      String[] list = paths.replace('\\', '/').split("\n");
      android.media.MediaScannerConnection.scanFile(MainActivity.this, list, null, null);
    }

    /**
     * Saves a picture the page made (a collage of the gallery's play screen, a JPEG in base64) as a new
     * file in Pictures/Choosen. Only adds that file; nothing else is written and nothing is sent.
     */
    @JavascriptInterface
    public boolean saveImage(String base64, String name) {
      if (!ownPage || base64 == null || base64.length() > 40_000_000) return false;
      String file = name != null && name.matches("[A-Za-z0-9._-]{1,80}\\.jpg") ? name : "Choosen-collage.jpg";
      Uri uri = null;
      try {
        byte[] jpg = android.util.Base64.decode(base64, android.util.Base64.DEFAULT);
        if (jpg.length < 4 || (jpg[0] & 0xFF) != 0xFF || (jpg[1] & 0xFF) != 0xD8) return false;  // a JPEG only
        android.content.ContentValues v = new android.content.ContentValues();
        v.put(MediaStore.Images.Media.DISPLAY_NAME, file);
        v.put(MediaStore.Images.Media.MIME_TYPE, "image/jpeg");
        v.put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/Choosen");
        v.put(MediaStore.Images.Media.IS_PENDING, 1);
        uri = getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, v);
        if (uri == null) return false;
        try (OutputStream o = getContentResolver().openOutputStream(uri)) {
          if (o == null) throw new IOException("no stream");
          o.write(jpg);
        }
        v.clear();
        v.put(MediaStore.Images.Media.IS_PENDING, 0);
        getContentResolver().update(uri, v, null, null);
        return true;
      } catch (IOException | RuntimeException e) {
        if (uri != null) getContentResolver().delete(uri, null, null);  // no half-written file
        return false;
      }
    }
  }

  /**
   * A PC address as the owner typed it, checked: an IPv4 address on the home network (private
   * ranges, or 127.0.0.1) with an optional port (Choosen's phone port is 47801 by default). "" if it is not one.
   */
  static String pcAddress(String addr) {
    if (addr == null) return "";
    String a = addr.trim().replaceFirst("^https?://", "").replaceFirst("/.*$", "");
    java.util.regex.Matcher m = java.util.regex.Pattern.compile("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})(?::(\\d{1,5}))?$").matcher(a);
    if (!m.matches()) return "";
    int[] o = new int[4];
    for (int i = 0; i < 4; i++) {
      o[i] = Integer.parseInt(m.group(i + 1));
      if (o[i] > 255) return "";
    }
    // the home network (private ranges), or the phone itself (127.0.0.1: a PC reached over USB with
    // "adb reverse", used while developing)
    boolean home = o[0] == 10 || (o[0] == 192 && o[1] == 168) || (o[0] == 172 && o[1] >= 16 && o[1] <= 31)
        || (o[0] == 127 && o[1] == 0 && o[2] == 0 && o[3] == 1);
    if (!home) return "";
    int port = m.group(5) == null ? 47801 : Integer.parseInt(m.group(5));
    if (port < 1 || port > 65535) return "";
    return o[0] + "." + o[1] + "." + o[2] + "." + o[3] + ":" + port;
  }

  final class Client extends WebViewClient {
    @Override
    public void onPageStarted(WebView view, String url, Bitmap favicon) {
      ownPage = url != null && url.startsWith("https://" + HOST + "/");
      boolean v3 = url != null && url.contains("/v3/");
      if (v3 != v3Page) {
        v3Page = v3;
        frame.requestApplyInsets();  // the normal page gets the notch as padding again
      }
    }

    @Override
    public void onPageFinished(WebView view, String url) {
      sendSafeTop();
    }

    @Override
    public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest r) {
      Uri u = r.getUrl();
      if (!HOST.equals(u.getHost())) {
        // the one way out: the PC the owner connected to (its own server answers); nothing else
        String pc = pcHost;
        if (!pc.isEmpty() && "http".equals(u.getScheme()) && pc.equals(u.getHost() + ":" + (u.getPort() < 0 ? 80 : u.getPort())))
          return null;
        return text(403, "{}");
      }
      String path = u.getPath() == null ? "/" : u.getPath();
      String query = u.getEncodedQuery() == null ? "" : u.getEncodedQuery();
      try {
        if (path.equals("/")) return asset("index.html");
        if (path.equals("/v3") || path.equals("/v3/") || path.equals("/v3/index.html")) return appPage();
        if (path.equals("/v3/app.js") || path.equals("/v3/app.css")) return asset(path.substring(1));
        if (path.startsWith("/img/")) return image(u, path.substring(5), false);
        if (path.startsWith("/thumb/")) return image(u, path.substring(7), true);
        if (path.equals("/api/events")) return new WebResourceResponse("text/event-stream", "utf-8", 200, "OK", noStore(), new Events());
        if (path.equals("/api/pickfolder")) return text(200, "{\"path\":" + jsonString(pickFolder()) + "}");
        if (path.equals("/api/dirs")) return text(200, listDirs(u.getQueryParameter("path")));
        if (path.startsWith("/api/")) {
          String res = Core.api(r.getMethod(), path, query, "");
          int nl = res.indexOf('\n');
          return text(Integer.parseInt(res.substring(0, nl)), res.substring(nl + 1));
        }
        if (path.lastIndexOf('/') == 0) return asset(path.substring(1));
      } catch (IOException | RuntimeException e) {
        return text(500, "{\"error\":\"internal\"}");
      }
      return text(404, "{}");
    }
  }

  private WebResourceResponse asset(String name) throws IOException {
    return new WebResourceResponse(mime(name), "utf-8", 200, "OK", noStore(), getAssets().open(name));
  }

  /** Choosen V3's page marked as the app's own (class="app"): its launch screen shows at once. */
  private WebResourceResponse appPage() throws IOException {
    String html;
    try (InputStream in = getAssets().open("v3/index.html")) {
      ByteArrayOutputStream b = new ByteArrayOutputStream();
      byte[] buf = new byte[16384];
      for (int n; (n = in.read(buf)) > 0; ) b.write(buf, 0, n);
      html = b.toString("UTF-8");
    }
    html = html.replaceFirst("<html lang=\"ja\">", "<html lang=\"ja\" class=\"app\">");
    return new WebResourceResponse("text/html", "utf-8", 200, "OK", noStore(),
        new ByteArrayInputStream(html.getBytes(StandardCharsets.UTF_8)));
  }

  /**
   * /img/<id>?v=<fp> streams the file; /img/<id>?v=<fp>&w=<px> returns a JPEG fitted to px (long side,
   * for Choosen V3: a 24 MP original would need ~96 MB in the WebView); /thumb/<id>?v=<fp>&s=<px>
   * returns a JPEG of about s px.
   */
  private WebResourceResponse image(Uri u, String idPart, boolean thumb) throws IOException {
    int id;
    try {
      id = Integer.parseInt(idPart);
    } catch (NumberFormatException e) {
      return text(404, "{}");
    }
    String file = Core.itemPath(id, u.getQueryParameter("v") == null ? "" : u.getQueryParameter("v"));
    if (file.isEmpty()) return text(404, "{}");
    String ext = file.substring(file.lastIndexOf('.') + 1).toLowerCase(Locale.ROOT);
    int fit = 0;
    if (!thumb && !"gif".equals(ext) && u.getQueryParameter("w") != null) {  // animated GIFs stay originals
      try {
        fit = Math.max(480, Math.min(2560, Integer.parseInt(u.getQueryParameter("w"))));
      } catch (NumberFormatException ignored) {
        // no fit: the original
      }
    }
    if (fit > 0) {
      byte[] jpg = null;
      fitDecodes.acquireUninterruptibly();
      try {
        jpg = fittedJpeg(file, fit);
      } finally {
        fitDecodes.release();
      }
      if (jpg != null) return new WebResourceResponse("image/jpeg", null, 200, "OK", noStore(), new ByteArrayInputStream(jpg));
    }
    if (!thumb && isBrowserFormat(ext)) return new WebResourceResponse(mime(file), null, 200, "OK", noStore(), new FileInputStream(file));
    int size = 2560;
    if (thumb) {
      String s = u.getQueryParameter("s");
      size = Math.max(64, Math.min(1024, s == null ? 320 : Integer.parseInt(s)));
    }
    String key = file + "|" + size;
    byte[] jpg = thumb ? thumbs.get(key) : null;
    if (jpg == null) {
      jpg = scaledJpeg(file, size, !thumb);
      if (jpg == null) return text(404, "{}");
      if (thumb) thumbs.put(key, jpg);
    }
    return new WebResourceResponse("image/jpeg", null, 200, "OK", noStore(), new ByteArrayInputStream(jpg));
  }

  /** Decodes an image straight at its long side fitted to `size` px (never larger), upright; null if it cannot. */
  private static byte[] fittedJpeg(String file, int size) {
    try {
      Bitmap b = ImageDecoder.decodeBitmap(ImageDecoder.createSource(new File(file)), (dec, info, src) -> {
        int w = info.getSize().getWidth(), h = info.getSize().getHeight();
        float k = Math.min(1f, (float) size / Math.max(w, h));
        dec.setTargetSize(Math.max(1, Math.round(w * k)), Math.max(1, Math.round(h * k)));
        dec.setAllocator(ImageDecoder.ALLOCATOR_SOFTWARE);  // compress() needs a software bitmap
      });
      ByteArrayOutputStream out = new ByteArrayOutputStream();
      b.compress(Bitmap.CompressFormat.JPEG, 88, out);
      b.recycle();
      return out.toByteArray();
    } catch (IOException | RuntimeException e) {
      return null;
    }
  }

  /** Decodes an image at about `size` px (short side for thumbnails, long side for full images), upright. */
  private static byte[] scaledJpeg(String file, int size, boolean longSide) {
    BitmapFactory.Options o = new BitmapFactory.Options();
    o.inJustDecodeBounds = true;
    BitmapFactory.decodeFile(file, o);
    if (o.outWidth <= 0 || o.outHeight <= 0) return null;
    int edge = longSide ? Math.max(o.outWidth, o.outHeight) : Math.min(o.outWidth, o.outHeight);
    int sample = 1;
    while (edge / (sample * 2) >= size) sample *= 2;
    o = new BitmapFactory.Options();
    o.inSampleSize = sample;
    Bitmap b = BitmapFactory.decodeFile(file, o);
    if (b == null) return null;
    int rotate = 0;
    try {
      int or = new ExifInterface(file).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL);
      rotate = or == ExifInterface.ORIENTATION_ROTATE_90 ? 90 : or == ExifInterface.ORIENTATION_ROTATE_180 ? 180
             : or == ExifInterface.ORIENTATION_ROTATE_270 ? 270 : 0;
    } catch (IOException ignored) {
      // no EXIF: keep as decoded
    }
    if (rotate != 0) {
      Matrix m = new Matrix();
      m.postRotate(rotate);
      Bitmap r = Bitmap.createBitmap(b, 0, 0, b.getWidth(), b.getHeight(), m, true);
      if (r != b) b.recycle();
      b = r;
    }
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    b.compress(Bitmap.CompressFormat.JPEG, 85, out);
    b.recycle();
    return out.toByteArray();
  }

  /** Server-Sent Events like the Windows server: the state whenever it changes, a ping every 15 s. */
  static final class Events extends InputStream {
    private byte[] buf = new byte[0];
    private int pos;
    private long sent = -1, lastSend;
    private volatile boolean closed;

    private boolean fill() {
      while (!closed) {
        long v = Core.version();
        long now = System.currentTimeMillis();
        String msg = null;
        if (v != sent) {
          String res = Core.api("GET", "/api/state", "", "");
          msg = "data: " + res.substring(res.indexOf('\n') + 1) + "\n\n";
          sent = v;
        } else if (now - lastSend > 15000) {
          msg = ": ping\n\n";
        }
        if (msg != null) {
          buf = msg.getBytes(StandardCharsets.UTF_8);
          pos = 0;
          lastSend = now;
          return true;
        }
        try {
          Thread.sleep(150);
        } catch (InterruptedException e) {
          return false;
        }
      }
      return false;
    }

    @Override
    public int read() {
      if (pos >= buf.length && !fill()) return -1;
      return buf[pos++] & 0xff;
    }

    @Override
    public int read(byte[] b, int off, int len) {
      if (pos >= buf.length && !fill()) return -1;
      int n = Math.min(len, buf.length - pos);
      System.arraycopy(buf, pos, b, off, n);
      pos += n;
      return n;
    }

    @Override
    public void close() {
      closed = true;
    }
  }

  // ---- small helpers -------------------------------------------------------------------------

  private static WebResourceResponse text(int status, String json) {
    String reason = status == 200 ? "OK" : status == 404 ? "Not Found" : status == 403 ? "Forbidden" : "Error";
    return new WebResourceResponse("application/json", "utf-8", status, reason, noStore(),
        new ByteArrayInputStream(json.getBytes(StandardCharsets.UTF_8)));
  }

  private static Map<String, String> noStore() {
    Map<String, String> h = new HashMap<>();
    h.put("Cache-Control", "no-store");
    return h;
  }

  private static boolean isBrowserFormat(String ext) {
    for (String f : BROWSER_FORMATS)
      if (f.equals(ext)) return true;
    return false;
  }

  private static String mime(String name) {
    String e = name.substring(name.lastIndexOf('.') + 1).toLowerCase(Locale.ROOT);
    switch (e) {
      case "html": return "text/html";
      case "js": return "text/javascript";
      case "css": return "text/css";
      case "png": return "image/png";
      case "jpg": case "jpeg": case "jfif": return "image/jpeg";
      case "gif": return "image/gif";
      case "webp": return "image/webp";
      case "avif": return "image/avif";
      case "bmp": return "image/bmp";
      default: return "application/octet-stream";
    }
  }

  private static String jsonString(String s) {
    StringBuilder b = new StringBuilder("\"");
    for (char c : s.toCharArray()) {
      if (c == '"' || c == '\\') b.append('\\').append(c);
      else if (c < 0x20) b.append(String.format(Locale.ROOT, "\\u%04x", (int) c));
      else b.append(c);
    }
    return b.append('"').toString();
  }

  /** A launcher alias of this app (AndroidManifest.xml), also in the QA build (another package). */
  private android.content.ComponentName alias(String name) {
    return new android.content.ComponentName(getPackageName(), "com.choosen.app." + name);
  }

  /**
   * Asks for the screen's fastest refresh rate at its current resolution (120 Hz on most current
   * phones), so swipes, scrolling and play move as smoothly as the screen allows. Android otherwise
   * keeps an app like this at 60 Hz. The phone's own display setting still has the last word.
   */
  private void preferFastestRefresh() {
    @SuppressWarnings("deprecation") // Android 10 has no Activity.getDisplay()
    android.view.Display display = Build.VERSION.SDK_INT >= 30 ? getDisplay() : getWindowManager().getDefaultDisplay();
    if (display == null) return;
    android.view.Display.Mode now = display.getMode(), best = now;
    for (android.view.Display.Mode m : display.getSupportedModes())
      if (m.getPhysicalWidth() == now.getPhysicalWidth() && m.getPhysicalHeight() == now.getPhysicalHeight()
          && m.getRefreshRate() > best.getRefreshRate()) best = m;
    android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
    lp.preferredDisplayModeId = best.getModeId();
    getWindow().setAttributes(lp);
  }
}
