package com.mhgu.recomp;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/** Settings -> System -> App version & updates. Downloads only a newer APK from this repository's latest release. */
public final class AppUpdateActivity extends Activity {
    private static final String TAG = "MHGU";
    private static final String RELEASE_API =
            "https://api.github.com/repos/bkotit5-cmd/gu-native/releases/latest";
    private static final String RELEASES_PAGE =
            "https://github.com/bkotit5-cmd/gu-native/releases/latest";
    private static final String APK_MIME = "application/vnd.android.package-archive";
    private static final int REQUEST_INSTALL_PERMISSION = 7412;

    private static final int WINE = Color.rgb(38, 18, 14);
    private static final int PANEL = Color.rgb(62, 38, 22);
    private static final int CREAM = Color.rgb(240, 228, 200);
    private static final int GOLD = Color.rgb(255, 255, 255);
    private static final int ALERT = Color.rgb(235, 150, 125);
    private static final int EDGE = Color.rgb(196, 150, 70);

    private TextView version, status, latest, notes;
    private Button checkButton, installButton, releaseButton;
    private ProgressBar progress;
    private Release release;
    private File downloadedApk;
    private String currentVersion;
    private long currentVersionCode;
    private boolean checking, downloading, installReady, waitingInstallPermission;

    private static final class Release {
        String tag, title, body, pageUrl, apkUrl, apkName, digest;
        long apkSize;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        readInstalledVersion();
        buildScreen();
        checkForUpdates();
    }

    private void readInstalledVersion() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            currentVersion = info.versionName == null ? "unknown" : info.versionName;
            currentVersionCode = info.getLongVersionCode();
        } catch (PackageManager.NameNotFoundException e) {
            currentVersion = "unknown";
            currentVersionCode = 0;
        }
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private GradientDrawable plate(int fill, int radius) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setStroke(dp(1), EDGE);
        d.setCornerRadius(dp(radius));
        return d;
    }

    private TextView text(String value, int size, int colour, boolean bold) {
        TextView v = new TextView(this);
        v.setText(value);
        v.setTextSize(size);
        v.setTextColor(colour);
        if (bold) v.setTypeface(Typeface.DEFAULT_BOLD);
        v.setLineSpacing(dp(2), 1f);
        return v;
    }

    private Button button(String label, boolean primary) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(14);
        b.setTextColor(primary ? GOLD : CREAM);
        if (primary) b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(plate(PANEL, 6));
        b.setPadding(dp(12), 0, dp(12), 0);
        return b;
    }

    private void buildScreen() {
        int pad = dp(16);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(pad * 2, pad, pad * 2, pad);

        TextView heading = text("APP VERSION & UPDATES", 20, GOLD, true);
        heading.setGravity(Gravity.CENTER);
        page.addView(heading);

        version = text("Installed version: " + currentVersion + "  ·  version code " + currentVersionCode,
                15, CREAM, false);
        version.setGravity(Gravity.CENTER);
        version.setPadding(0, dp(8), 0, dp(14));
        page.addView(version);

        status = text("Checking GitHub Releases…", 15, GOLD, true);
        status.setGravity(Gravity.CENTER);
        status.setPadding(dp(12), dp(10), dp(12), dp(10));
        status.setBackground(plate(PANEL, 6));
        page.addView(status);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(100);
        progress.setProgress(0);
        progress.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressSize = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(8));
        progressSize.topMargin = dp(8);
        page.addView(progress, progressSize);

        latest = text("", 16, GOLD, true);
        latest.setPadding(0, dp(14), 0, dp(6));
        page.addView(latest);

        notes = text("", 13, CREAM, false);
        notes.setPadding(dp(12), dp(10), dp(12), dp(10));
        notes.setBackground(plate(WINE, 6));
        notes.setTextIsSelectable(true);
        page.addView(notes);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(page);
        LinearLayout whole = new LinearLayout(this);
        whole.setOrientation(LinearLayout.VERTICAL);
        whole.setBackgroundColor(WINE);
        whole.addView(scroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        LinearLayout actions = new LinearLayout(this);
        actions.setGravity(Gravity.CENTER);
        actions.setPadding(dp(6), dp(6), dp(6), dp(10));
        checkButton = button("Check again", false);
        checkButton.setOnClickListener(v -> checkForUpdates());
        installButton = button("Download & install", true);
        installButton.setEnabled(false);
        installButton.setOnClickListener(v -> {
            if (installReady) launchInstaller();
            else downloadUpdate();
        });
        releaseButton = button("Open release page", false);
        releaseButton.setEnabled(false);
        releaseButton.setOnClickListener(v -> openReleasePage());
        Button close = button("Close", false);
        close.setOnClickListener(v -> finish());
        addAction(actions, checkButton);
        addAction(actions, installButton);
        addAction(actions, releaseButton);
        addAction(actions, close);
        whole.addView(actions);
        whole.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets;
        });
        setContentView(whole);
    }

    private void addAction(LinearLayout row, Button button) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(44), 1);
        lp.setMargins(dp(3), 0, dp(3), 0);
        row.addView(button, lp);
    }

    private void setStatus(String message, boolean warning) {
        status.setText(message);
        status.setTextColor(warning ? ALERT : GOLD);
    }

    private void checkForUpdates() {
        if (checking || downloading) return;
        checking = true;
        release = null;
        installReady = false;
        installButton.setEnabled(false);
        releaseButton.setEnabled(false);
        latest.setText("");
        notes.setText("");
        setStatus("Checking the latest release…", false);
        checkButton.setEnabled(false);

        new Thread(() -> {
            HttpURLConnection http = null;
            try {
                http = (HttpURLConnection) new URL(RELEASE_API).openConnection();
                http.setConnectTimeout(10000);
                http.setReadTimeout(15000);
                http.setRequestProperty("Accept", "application/vnd.github+json");
                http.setRequestProperty("User-Agent", "bkotit5-cmd-gu-native-updater");
                int code = http.getResponseCode();
                if (code == 404) {
                    runOnUiThread(() -> finishCheck(null,
                            "No stable release has been published to this repository yet.", false));
                    return;
                }
                if (code != HttpURLConnection.HTTP_OK)
                    throw new java.io.IOException("GitHub returned HTTP " + code);
                StringBuilder body = new StringBuilder();
                try (InputStream in = http.getInputStream()) {
                    byte[] buffer = new byte[8192];
                    int n;
                    while ((n = in.read(buffer)) > 0 && body.length() < (256 << 10))
                        body.append(new String(buffer, 0, n, java.nio.charset.StandardCharsets.UTF_8));
                }
                Release found = parseRelease(new JSONObject(body.toString()));
                if (found == null)
                    throw new java.io.IOException("The release information is incomplete.");
                if (!Notices.newer(found.tag, currentVersion)) {
                    runOnUiThread(() -> finishCheck(null,
                            "You are up to date. Installed version: " + currentVersion, false));
                } else {
                    runOnUiThread(() -> finishCheck(found,
                            found.apkUrl == null
                                    ? "A newer version was published, but this release has no APK attached."
                                    : "Update available: " + cleanVersion(found.tag), found.apkUrl == null));
                }
            } catch (Exception e) {
                LogWarning.write("update check: " + e);
                runOnUiThread(() -> finishCheck(null,
                        "Could not check GitHub Releases: " + e.getMessage(), true));
            } finally {
                if (http != null) http.disconnect();
            }
        }, "app-update-check").start();
    }

    private Release parseRelease(JSONObject json) throws Exception {
        Release r = new Release();
        r.tag = json.optString("tag_name", "");
        r.title = json.optString("name", r.tag);
        r.body = json.optString("body", "").trim();
        r.pageUrl = json.optString("html_url", RELEASES_PAGE);
        if (!r.pageUrl.startsWith("https://github.com/bkotit5-cmd/gu-native/releases/"))
            r.pageUrl = RELEASES_PAGE;

        JSONArray assets = json.optJSONArray("assets");
        if (assets != null) {
            JSONObject preferred = null, fallback = null;
            for (int i = 0; i < assets.length(); i++) {
                JSONObject asset = assets.optJSONObject(i);
                if (asset == null) continue;
                String name = asset.optString("name", "");
                String url = asset.optString("browser_download_url", "");
                if (!name.toLowerCase(Locale.ROOT).endsWith(".apk") ||
                        !url.startsWith("https://github.com/bkotit5-cmd/gu-native/releases/download/"))
                    continue;
                if (fallback == null) fallback = asset;
                String lower = name.toLowerCase(Locale.ROOT);
                if (lower.contains("arm64") || lower.contains("arm64-v8a")) {
                    preferred = asset;
                    break;
                }
            }
            JSONObject asset = preferred != null ? preferred : fallback;
            if (asset != null) {
                r.apkName = asset.optString("name", "app-release.apk");
                r.apkUrl = asset.optString("browser_download_url", null);
                r.apkSize = asset.optLong("size", 0);
                r.digest = asset.optString("digest", "");
            }
        }
        return r.tag.isEmpty() ? null : r;
    }

    private void finishCheck(Release found, String message, boolean noApk) {
        checking = false;
        checkButton.setEnabled(true);
        setStatus(message, noApk);
        if (found == null) return;
        release = found;
        latest.setText((found.title.isEmpty() ? "Version " : found.title + "\n") +
                "Version " + cleanVersion(found.tag));
        notes.setText(found.body.isEmpty() ? "No release notes were provided." : found.body);
        releaseButton.setEnabled(true);
        installButton.setEnabled(found.apkUrl != null);
    }

    private void downloadUpdate() {
        if (release == null || release.apkUrl == null || downloading || checking) return;
        downloading = true;
        checkButton.setEnabled(false);
        installButton.setEnabled(false);
        releaseButton.setEnabled(false);
        progress.setProgress(0);
        progress.setVisibility(View.VISIBLE);
        setStatus("Downloading " + release.apkName + "…", false);
        final Release target = release;

        new Thread(() -> {
            HttpURLConnection http = null;
            File temp = null;
            try {
                File dir = new File(getCacheDir(), "updates");
                if (!dir.isDirectory() && !dir.mkdirs())
                    throw new java.io.IOException("Cannot create the update cache.");
                temp = new File(dir, "app-update.apk.part");
                File finalApk = new File(dir, "app-update.apk");
                http = (HttpURLConnection) new URL(target.apkUrl).openConnection();
                http.setConnectTimeout(15000);
                http.setReadTimeout(30000);
                http.setRequestProperty("User-Agent", "bkotit5-cmd-gu-native-updater");
                int code = http.getResponseCode();
                if (code != HttpURLConnection.HTTP_OK)
                    throw new java.io.IOException("APK download returned HTTP " + code);

                MessageDigest digest = MessageDigest.getInstance("SHA-256");
                long written = 0, expected = target.apkSize > 0 ? target.apkSize : http.getContentLengthLong();
                long lastUi = 0;
                try (InputStream in = http.getInputStream(); FileOutputStream out = new FileOutputStream(temp)) {
                    byte[] buffer = new byte[64 * 1024];
                    int n;
                    while ((n = in.read(buffer)) != -1) {
                        out.write(buffer, 0, n);
                        digest.update(buffer, 0, n);
                        written += n;
                        long now = System.currentTimeMillis();
                        if (now - lastUi > 120 && expected > 0) {
                            lastUi = now;
                            int value = (int) Math.min(100, written * 100 / expected);
                            runOnUiThread(() -> progress.setProgress(value));
                        }
                    }
                    out.getFD().sync();
                }
                if (written < 1024 || (expected > 0 && written != expected))
                    throw new java.io.IOException("The downloaded APK is incomplete.");
                if (!target.digest.isEmpty() && target.digest.startsWith("sha256:")) {
                    String actual = hex(digest.digest());
                    String wanted = target.digest.substring("sha256:".length());
                    if (!actual.equalsIgnoreCase(wanted))
                        throw new java.io.IOException("The APK SHA-256 does not match the release checksum.");
                }

                PackageInfo archive = getPackageManager().getPackageArchiveInfo(
                        temp.getAbsolutePath(), PackageManager.GET_SIGNING_CERTIFICATES);
                PackageInfo installed = getPackageManager().getPackageInfo(
                        getPackageName(), PackageManager.GET_SIGNING_CERTIFICATES);
                if (archive == null || !getPackageName().equals(archive.packageName))
                    throw new java.io.IOException("The downloaded file is not an APK for this app.");
                if (archive.versionName == null || !Notices.newer(archive.versionName, currentVersion) ||
                        archive.getLongVersionCode() <= installed.getLongVersionCode())
                    throw new java.io.IOException("The APK version or version code is not newer than the installed app.");
                if (!sameSigningCertificate(installed, archive))
                    throw new java.io.IOException("This APK uses a different signing certificate, so Android cannot install it over this app. "
                            + "To switch builds, export your save from Settings > System > Your game > Export save, uninstall this app, "
                            + "install the APK, then import your save.");

                if (finalApk.exists() && !finalApk.delete())
                    throw new java.io.IOException("Cannot replace the cached APK.");
                if (!temp.renameTo(finalApk))
                    throw new java.io.IOException("Cannot finish saving the downloaded APK.");
                runOnUiThread(() -> {
                    downloading = false;
                    installReady = true;
                    downloadedApk = finalApk;
                    progress.setProgress(100);
                    setStatus("Download verified. Ready to install version " + archive.versionName + ".", false);
                    checkButton.setEnabled(true);
                    installButton.setEnabled(true);
                    installButton.setText("Install update");
                    releaseButton.setEnabled(true);
                    launchInstaller();
                });
            } catch (Exception e) {
                if (temp != null) temp.delete();
                LogWarning.write("update download: " + e);
                runOnUiThread(() -> {
                    downloading = false;
                    progress.setVisibility(View.GONE);
                    setStatus("Update was not installed: " + e.getMessage(), true);
                    checkButton.setEnabled(true);
                    installButton.setEnabled(release != null && release.apkUrl != null);
                    releaseButton.setEnabled(release != null);
                });
            } finally {
                if (http != null) http.disconnect();
            }
        }, "app-update-download").start();
    }

    private boolean sameSigningCertificate(PackageInfo installed, PackageInfo archive) throws Exception {
        return signerFingerprints(installed).equals(signerFingerprints(archive));
    }

    private Set<String> signerFingerprints(PackageInfo info) throws Exception {
        Set<String> result = new HashSet<>();
        if (info.signingInfo == null) return result;
        Signature[] signers = info.signingInfo.getApkContentsSigners();
        if (signers == null) return result;
        MessageDigest sha = MessageDigest.getInstance("SHA-256");
        for (Signature signature : signers) result.add(hex(sha.digest(signature.toByteArray())));
        return result;
    }

    private static String hex(byte[] bytes) {
        StringBuilder out = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) out.append(String.format(Locale.ROOT, "%02x", b & 0xff));
        return out.toString();
    }

    private static String cleanVersion(String tag) {
        return tag.startsWith("v") || tag.startsWith("V") ? tag.substring(1) : tag;
    }

    private void launchInstaller() {
        if (!installReady || downloadedApk == null || !downloadedApk.isFile()) return;
        if (Build.VERSION.SDK_INT >= 26 && !getPackageManager().canRequestPackageInstalls()) {
            waitingInstallPermission = true;
            try {
                startActivityForResult(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + getPackageName())), REQUEST_INSTALL_PERMISSION);
            } catch (ActivityNotFoundException e) {
                setStatus("Android does not provide the permission screen for installing APK files.", true);
            }
            return;
        }
        try {
            Uri apk = Uri.parse("content://" + getPackageName() + ".updatefiles/apk/app-update.apk");
            Intent installer = new Intent(Intent.ACTION_VIEW);
            installer.setDataAndType(apk, APK_MIME);
            installer.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(installer);
        } catch (ActivityNotFoundException e) {
            setStatus("No Android package installer was found. Open the release page to install the APK manually.", true);
        } catch (SecurityException e) {
            setStatus("Android blocked the APK installer. Check the install-unknown-apps permission.", true);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_INSTALL_PERMISSION) {
            waitingInstallPermission = false;
            if (Build.VERSION.SDK_INT >= 26 && getPackageManager().canRequestPackageInstalls())
                launchInstaller();
            else
                setStatus("Allow this app to install updates, then press Install update again.", true);
        }
    }

    private void openReleasePage() {
        String target = release != null && release.pageUrl != null ? release.pageUrl : RELEASES_PAGE;
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(target)));
        } catch (ActivityNotFoundException e) {
            setStatus("No browser is available to open the release page.", true);
        }
    }

    private static final class LogWarning {
        static void write(String message) {
            android.util.Log.w(TAG, message);
        }
    }
}
