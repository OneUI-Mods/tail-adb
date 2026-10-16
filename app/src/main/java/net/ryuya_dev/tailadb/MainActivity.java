package net.ryuya_dev.tailadb;

import android.app.LocaleManager;
import android.content.*;
import android.graphics.Typeface;
import android.os.*;
import android.view.*;
import android.widget.*;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.*;
import androidx.core.widget.NestedScrollView;
import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.bottomnavigation.BottomNavigationView;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.chip.Chip;
import com.google.android.material.color.*;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.CircularProgressIndicator;
import com.google.android.material.textfield.*;
import com.google.android.material.textview.MaterialTextView;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;

public final class MainActivity extends AppCompatActivity {
  private LinearLayout root, connection, settings;
  private MaterialTextView status, endpoint, rootState, peerState, allowedHosts;
  private Chip badge;
  private ImageView shield;
  private CircularProgressIndicator progress;
  private MaterialButton start, stop, copy, allowPeer, revokePeer;
  private TextInputLayout portLayout;
  private TextInputEditText port;
  private NestedScrollView connectScroll, settingsScroll;
  private static boolean requestedRoot;
  private static volatile boolean rootAvailable;
  private boolean registered;
  private ScheduledExecutorService statusRefresh;
  private final BroadcastReceiver receiver =
      new BroadcastReceiver() {
        @Override
        public void onReceive(Context c, Intent i) {
          render();
        }
      };

  @Override
  protected void onCreate(Bundle saved) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(saved);
    root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);
    ViewCompat.setOnApplyWindowInsetsListener(
        root,
        (v, insets) -> {
          var bars =
              insets.getInsets(
                  WindowInsetsCompat.Type.systemBars() | WindowInsetsCompat.Type.displayCutout());
          v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
          return insets;
        });
    setContentView(root);
    var controller = WindowCompat.getInsetsController(getWindow(), root);
    boolean light = (getResources().getConfiguration().uiMode & 0x30) != 0x20;
    controller.setAppearanceLightStatusBars(light);
    controller.setAppearanceLightNavigationBars(light);
    MaterialToolbar toolbar = new MaterialToolbar(this);
    toolbar.setTitle(R.string.app_name);
    root.addView(toolbar, new LinearLayout.LayoutParams(-1, dp(64)));
    FrameLayout content = new FrameLayout(this);
    root.addView(content, new LinearLayout.LayoutParams(-1, 0, 1));
    connectScroll = scroll();
    settingsScroll = scroll();
    connection = column();
    settings = column();
    connectScroll.addView(connection);
    settingsScroll.addView(settings);
    content.addView(connectScroll);
    content.addView(settingsScroll);
    settingsScroll.setVisibility(View.GONE);
    buildConnection();
    buildSettings();
    BottomNavigationView nav = new BottomNavigationView(this);
    nav.getMenu().add(0, 1, 0, R.string.connection).setIcon(R.drawable.ic_terminal);
    nav.getMenu().add(0, 2, 1, R.string.settings).setIcon(R.drawable.ic_shield);
    root.addView(nav, new LinearLayout.LayoutParams(-1, -2));
    nav.setOnItemSelectedListener(
        item -> {
          connectScroll.setVisibility(item.getItemId() == 1 ? View.VISIBLE : View.GONE);
          settingsScroll.setVisibility(item.getItemId() == 2 ? View.VISIBLE : View.GONE);
          return true;
        });
    if (!TailService.running) {
      String old = State.prefs(this).getString("state", "STOPPED");
      if (!old.startsWith("ERROR_")) State.set(this, "STOPPED", "");
    }
    render();
    if (!requestedRoot && !TailService.running) {
      requestedRoot = true;
      requestRoot();
    }
  }

  private void buildConnection() {
    LinearLayout panel = card(connection);
    shield = new ImageView(this);
    shield.setImageResource(R.drawable.ic_shield);
    shield.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
    panel.addView(shield, new LinearLayout.LayoutParams(dp(64), dp(64)));
    status = text("", 24);
    panel.addView(status);
    badge = new Chip(this);
    badge.setClickable(false);
    badge.setCheckable(false);
    panel.addView(badge);
    progress = new CircularProgressIndicator(this);
    progress.setIndeterminate(true);
    panel.addView(progress, new LinearLayout.LayoutParams(dp(32), dp(32)));
    panel.addView(text(getString(R.string.instructions), 16));
    LinearLayout address = card(connection);
    address.addView(text(getString(R.string.endpoint), 16));
    endpoint = text("", 18);
    endpoint.setTypeface(Typeface.MONOSPACE);
    endpoint.setTextIsSelectable(true);
    address.addView(endpoint);
    copy =
        button(
            R.string.copy,
            () -> {
              String detail = State.prefs(this).getString("detail", "");
              String value = detail.split("\t", -1)[0];
              if (!value.isBlank()) {
                getSystemService(ClipboardManager.class)
                    .setPrimaryClip(ClipData.newPlainText("ADB", "adb connect " + value));
                Toast.makeText(this, R.string.copied, Toast.LENGTH_SHORT).show();
              }
            });
    address.addView(copy);
    LinearLayout access = card(connection);
    access.addView(text(getString(R.string.connected_host), 16));
    peerState = text("—", 18);
    peerState.setTypeface(Typeface.MONOSPACE);
    access.addView(peerState);
    allowPeer = button(R.string.allow_root_host, () -> setCurrentPeerAllowed(true));
    access.addView(allowPeer, new LinearLayout.LayoutParams(-1, dp(56)));
    revokePeer = button(R.string.revoke_root_host, () -> setCurrentPeerAllowed(false));
    access.addView(revokePeer, new LinearLayout.LayoutParams(-1, dp(56)));
    portLayout = new TextInputLayout(this);
    portLayout.setHint(getString(R.string.port));
    portLayout.setBoxBackgroundMode(TextInputLayout.BOX_BACKGROUND_OUTLINE);
    port = new TextInputEditText(portLayout.getContext());
    port.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
    port.setText(
        String.valueOf(getSharedPreferences("settings", MODE_PRIVATE).getInt("port", 5555)));
    portLayout.addView(port);
    connection.addView(portLayout, new LinearLayout.LayoutParams(-1, -2));
    start =
        button(
            R.string.start,
            () -> {
              int number;
              try {
                number = Integer.parseInt(port.getText().toString());
                if (number < 1024 || number > 65535) throw new NumberFormatException();
              } catch (NumberFormatException e) {
                portLayout.setError(getString(R.string.port_error));
                return;
              }
              portLayout.setError(null);
              getSharedPreferences("settings", MODE_PRIVATE).edit().putInt("port", number).apply();
              startService(new Intent(this, TailService.class));
              render();
            });
    connection.addView(start, new LinearLayout.LayoutParams(-1, dp(56)));
    stop =
        button(
            R.string.stop,
            () -> startService(new Intent(this, TailService.class).setAction("STOP")));
    connection.addView(stop, new LinearLayout.LayoutParams(-1, dp(56)));
  }

  private void buildSettings() {
    LinearLayout security = card(settings);
    security.addView(text(getString(R.string.scope), 16));
    security.addView(text(getString(R.string.compatibility), 16));
    security.addView(text(getString(R.string.isolation), 16));
    security.addView(text(getString(R.string.root_host_explanation), 16));
    allowedHosts = text("", 16);
    allowedHosts.setTypeface(Typeface.MONOSPACE);
    security.addView(allowedHosts);
    LinearLayout permissions = card(settings);
    permissions.addView(text(getString(R.string.root_hint), 16));
    rootState = text(getString(R.string.root_checking), 16);
    permissions.addView(rootState);
    settings.addView(
        button(
            R.string.language,
            () -> {
              String[] labels = {getString(R.string.system_language), "English", "日本語"};
              String[] tags = {"", "en", "ja"};
              new MaterialAlertDialogBuilder(this)
                  .setTitle(R.string.choose_language)
                  .setItems(
                      labels,
                      (d, n) ->
                          getSystemService(LocaleManager.class)
                              .setApplicationLocales(LocaleList.forLanguageTags(tags[n])))
                  .show();
            }),
        new LinearLayout.LayoutParams(-1, dp(56)));
    settings.addView(
        button(R.string.licenses, () -> startActivity(new Intent(this, LicensesActivity.class))),
        new LinearLayout.LayoutParams(-1, dp(56)));
    settings.addView(text(getString(R.string.version, BuildConfig.VERSION_NAME), 14));
  }

  private void requestRoot() {
    new Thread(
            () -> {
              boolean allowed = false;
              try {
                java.lang.Process p =
                    new ProcessBuilder("su", "-c", "id").redirectErrorStream(true).start();
                if (p.waitFor(30, TimeUnit.SECONDS))
                  allowed =
                      p.exitValue() == 0
                          && new String(p.getInputStream().readAllBytes(), StandardCharsets.UTF_8)
                              .contains("uid=0(");
                else p.destroy();
              } catch (Exception ignored) {
              }
              boolean result = allowed;
              rootAvailable = result;
              runOnUiThread(
                  () -> {
                    if (!isDestroyed()) {
                      rootState.setText(result ? R.string.root_allowed : R.string.state_root);
                      if (!result && !TailService.running) State.set(this, "ERROR_ROOT", "");
                    }
                  });
            },
            "TailAdbRootCheck")
        .start();
  }

  private void setCurrentPeerAllowed(boolean allowed) {
    String peer = State.prefs(this).getString("peer", "");
    if (peer == null || peer.isBlank()) return;
    var preferences = getSharedPreferences("settings", MODE_PRIVATE);
    String saved = preferences.getString("allowed_hosts", "");
    TreeSet<String> hosts = new TreeSet<>();
    if (saved != null)
      for (String item : saved.split(","))
        if (!item.isBlank()) hosts.add(item);
    if (allowed) hosts.add(peer);
    else hosts.remove(peer);
    preferences.edit().putString("allowed_hosts", String.join(",", hosts)).apply();
    String state = State.prefs(this).getString("state", "STOPPED");
    if (!state.equals("STOPPED") && !state.startsWith("ERROR_"))
      startService(new Intent(this, TailService.class).setAction("RESTART"));
    render();
  }

  private void render() {
    String code = State.prefs(this).getString("state", "STOPPED"),
        detail = State.prefs(this).getString("detail", "");
    boolean active = code.equals("LISTENING"),
        busy = code.equals("STARTING") || code.equals("RETRYING") || code.equals("STOPPING"),
        backend = active || code.equals("WAITING_TAILSCALE") || code.equals("RETRYING");
    status.setText(R.string.connection);
    badge.setText(State.label(code));
    int icon =
        active
            ? R.drawable.ic_shield
            : (code.startsWith("ERROR_") ? R.drawable.ic_error : R.drawable.ic_clock);
    shield.setImageResource(icon);
    badge.setChipIconResource(icon);
    if (active) rootState.setText(R.string.root_allowed);
    int color =
        MaterialColors.getColor(
            shield,
            code.startsWith("ERROR_")
                ? android.R.attr.colorError
                : androidx.appcompat.R.attr.colorPrimary);
    shield.setColorFilter(color);
    progress.setVisibility(busy ? View.VISIBLE : View.GONE);
    String host = detail.split("\t", -1)[0];
    endpoint.setText(active ? "adb connect " + host : "—");
    copy.setEnabled(active);
    start.setEnabled(!TailService.running && !busy && !backend);
    stop.setEnabled(!TailService.running && backend);
    port.setEnabled(!TailService.running && !backend);
    String peer = State.prefs(this).getString("peer", "");
    boolean peerAllowed = State.prefs(this).getBoolean("peer_allowed", false);
    peerState.setText(
        peer == null || peer.isBlank()
            ? "—"
            : peer + "  ·  " + getString(peerAllowed ? R.string.root_enabled : R.string.shell_only));
    allowPeer.setEnabled(peer != null && !peer.isBlank() && !peerAllowed && !TailService.running);
    revokePeer.setEnabled(peer != null && !peer.isBlank() && peerAllowed && !TailService.running);
    String allowed = getSharedPreferences("settings", MODE_PRIVATE).getString("allowed_hosts", "");
    allowedHosts.setText(
        getString(R.string.allowed_hosts)
            + "\n"
            + (allowed == null || allowed.isBlank()
                ? getString(R.string.no_allowed_hosts)
                : allowed.replace(",", "\n")));
    badge.setOnClickListener(
        v ->
            new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.technical)
                .setMessage(code + (detail.isBlank() ? "" : "\n" + detail))
                .setPositiveButton(android.R.string.ok, null)
                .show());
  }

  @Override
  protected void onStart() {
    super.onStart();
    registerReceiver(receiver, new IntentFilter(State.ACTION), Context.RECEIVER_NOT_EXPORTED);
    registered = true;
    statusRefresh = Executors.newSingleThreadScheduledExecutor();
    statusRefresh.scheduleWithFixedDelay(
        () -> {
          if (rootAvailable) TailService.refreshState(getApplicationContext());
        },
        1,
        3,
        TimeUnit.SECONDS);
    render();
  }

  @Override
  protected void onStop() {
    if (registered) {
      unregisterReceiver(receiver);
      registered = false;
    }
    if (statusRefresh != null) {
      statusRefresh.shutdownNow();
      statusRefresh = null;
    }
    super.onStop();
  }

  private int dp(int value) {
    return Math.round(value * getResources().getDisplayMetrics().density);
  }

  private LinearLayout column() {
    LinearLayout box = new LinearLayout(this);
    box.setOrientation(LinearLayout.VERTICAL);
    box.setPadding(dp(20), dp(12), dp(20), dp(24));
    return box;
  }

  private NestedScrollView scroll() {
    NestedScrollView view = new NestedScrollView(this);
    view.setFillViewport(true);
    return view;
  }

  private LinearLayout card(LinearLayout parent) {
    MaterialCardView card = new MaterialCardView(this);
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
    lp.bottomMargin = dp(16);
    parent.addView(card, lp);
    LinearLayout inside = column();
    card.addView(inside);
    return inside;
  }

  private MaterialTextView text(String value, int size) {
    MaterialTextView t = new MaterialTextView(this);
    t.setText(value);
    t.setTextSize(size);
    t.setPadding(0, dp(6), 0, dp(10));
    return t;
  }

  private MaterialButton button(int label, Runnable action) {
    MaterialButton b = new MaterialButton(this);
    b.setText(label);
    b.setOnClickListener(v -> action.run());
    return b;
  }
}
