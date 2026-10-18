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
import com.google.android.material.materialswitch.MaterialSwitch;
import com.google.android.material.progressindicator.CircularProgressIndicator;
import com.google.android.material.textfield.*;
import com.google.android.material.textview.MaterialTextView;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;

public final class MainActivity extends AppCompatActivity {
  private LinearLayout root, connection, settings;
  private MaterialTextView status, endpoint, rootState, peerState;
  private Chip badge;
  private ImageView shield;
  private MaterialCardView vpnCandidatesCard;
  private MaterialTextView vpnCandidatesTitle, vpnCandidatesMessage;
  private LinearLayout vpnCandidatesList, authorizedHostsList;
  private CircularProgressIndicator progress;
  private MaterialButton start, stop, copy;
  private MaterialSwitch shellToggle, rootToggle;
  private MaterialButton vpnUser;
  private TextInputLayout portLayout;
  private TextInputEditText port;
  private NestedScrollView connectScroll, settingsScroll;
  private static boolean requestedRoot;
  private static volatile boolean rootAvailable;
  private boolean registered;
  private boolean renderingAccess;
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
    vpnCandidatesCard = new MaterialCardView(this);
    LinearLayout.LayoutParams candidatesParams = new LinearLayout.LayoutParams(-1, -2);
    candidatesParams.bottomMargin = dp(16);
    connection.addView(vpnCandidatesCard, candidatesParams);
    LinearLayout candidatesContent = column();
    vpnCandidatesCard.addView(candidatesContent);
    vpnCandidatesTitle = text("", 20);
    vpnCandidatesTitle.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    candidatesContent.addView(vpnCandidatesTitle);
    vpnCandidatesMessage = text("", 14);
    candidatesContent.addView(vpnCandidatesMessage);
    vpnCandidatesList = new LinearLayout(this);
    vpnCandidatesList.setOrientation(LinearLayout.VERTICAL);
    candidatesContent.addView(vpnCandidatesList, new LinearLayout.LayoutParams(-1, -2));
    vpnCandidatesCard.setVisibility(View.GONE);
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
    shellToggle = new MaterialSwitch(this);
    shellToggle.setText(R.string.shell_access);
    shellToggle.setOnCheckedChangeListener(
        (button, checked) -> {
          if (!renderingAccess) setCurrentPeerAccess("shell_hosts", checked);
        });
    access.addView(shellToggle, new LinearLayout.LayoutParams(-1, dp(56)));
    rootToggle = new MaterialSwitch(this);
    rootToggle.setText(R.string.root_access);
    rootToggle.setOnCheckedChangeListener(
        (button, checked) -> {
          if (!renderingAccess) setCurrentPeerAccess("allowed_hosts", checked);
        });
    access.addView(rootToggle, new LinearLayout.LayoutParams(-1, dp(56)));
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
    security.addView(text(getString(R.string.authorized_hosts), 20));
    security.addView(text(getString(R.string.authorized_hosts_explanation), 14));
    authorizedHostsList = new LinearLayout(this);
    authorizedHostsList.setOrientation(LinearLayout.VERTICAL);
    security.addView(authorizedHostsList, new LinearLayout.LayoutParams(-1, -2));
    LinearLayout startup = card(settings);
    LinearLayout vpn = card(settings);
    vpn.addView(text(getString(R.string.tailscale_user), 16));
    vpnUser = button(R.string.choose_tailscale_user, this::showVpnUserDialog);
    vpn.addView(vpnUser, new LinearLayout.LayoutParams(-1, dp(56)));
    vpn.addView(text(getString(R.string.tailscale_user_explanation), 14));
    MaterialSwitch autoStart = new MaterialSwitch(this);
    autoStart.setText(R.string.auto_start);
    autoStart.setChecked(BootReceiver.isEnabled(this));
    autoStart.setOnCheckedChangeListener(
        (button, checked) -> BootReceiver.setEnabled(this, checked));
    startup.addView(autoStart, new LinearLayout.LayoutParams(-1, dp(56)));
    startup.addView(text(getString(R.string.auto_start_explanation), 14));
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
                      if (result) TailService.refreshState(getApplicationContext());
                      if (!result && !TailService.running) State.set(this, "ERROR_ROOT", "");
                    }
                  });
            },
            "TailAdbRootCheck")
        .start();
  }

  private void setCurrentPeerAccess(String preference, boolean allowed) {
    String peer = State.prefs(this).getString("peer", "");
    if (peer == null || peer.isBlank()) return;
    setHostAccess(peer, preference, allowed);
  }

  private void setHostAccess(String peer, String preference, boolean allowed) {
    var preferences = getSharedPreferences("settings", MODE_PRIVATE);
    String saved = preferences.getString(preference, "");
    TreeSet<String> hosts = new TreeSet<>();
    if (saved != null) for (String item : saved.split(",")) if (!item.isBlank()) hosts.add(item);
    if (allowed) hosts.add(peer);
    else hosts.remove(peer);
    var edit = preferences.edit().putString(preference, String.join(",", hosts));
    if (preference.equals("allowed_hosts") && allowed) {
      TreeSet<String> shell = hostSet(preferences.getString("shell_hosts", ""));
      shell.add(peer);
      edit.putString("shell_hosts", String.join(",", shell));
    } else if (preference.equals("shell_hosts") && !allowed) {
      TreeSet<String> root = hostSet(preferences.getString("allowed_hosts", ""));
      root.remove(peer);
      edit.putString("allowed_hosts", String.join(",", root));
    }
    edit.apply();
    String state = State.prefs(this).getString("state", "STOPPED");
    if (!state.equals("STOPPED") && !state.startsWith("ERROR_"))
      startService(new Intent(this, TailService.class).setAction("RESTART"));
    render();
  }

  private void renderAuthorizedHosts(TreeSet<String> shell, TreeSet<String> root) {
    authorizedHostsList.removeAllViews();
    TreeSet<String> all = new TreeSet<>(shell);
    all.addAll(root);
    if (all.isEmpty()) {
      authorizedHostsList.addView(text(getString(R.string.no_authorized_hosts), 16));
      return;
    }
    for (String host : all) {
      MaterialCardView row = new MaterialCardView(this);
      LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(-1, -2);
      rowParams.topMargin = dp(8);
      LinearLayout content = column();
      MaterialTextView address = text(host, 17);
      address.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
      content.addView(address);
      MaterialSwitch shellAccess = new MaterialSwitch(this);
      shellAccess.setText(R.string.shell_access);
      shellAccess.setChecked(shell.contains(host));
      shellAccess.setOnCheckedChangeListener(
          (button, checked) -> setHostAccess(host, "shell_hosts", checked));
      content.addView(shellAccess, new LinearLayout.LayoutParams(-1, dp(52)));
      MaterialSwitch rootAccess = new MaterialSwitch(this);
      rootAccess.setText(R.string.root_access);
      rootAccess.setChecked(root.contains(host));
      rootAccess.setOnCheckedChangeListener(
          (button, checked) -> setHostAccess(host, "allowed_hosts", checked));
      content.addView(rootAccess, new LinearLayout.LayoutParams(-1, dp(52)));
      row.addView(content);
      authorizedHostsList.addView(row, rowParams);
    }
  }

  private TreeSet<String> hostSet(String saved) {
    TreeSet<String> hosts = new TreeSet<>();
    if (saved != null) for (String item : saved.split(",")) if (!item.isBlank()) hosts.add(item);
    return hosts;
  }

  private int currentUserId() {
    return android.os.Process.myUid() / 100000;
  }

  private String userLabel(UserHandle handle) {
    int id = handle.hashCode();
    if (id == currentUserId()) return getString(R.string.current_user, id);
    CharSequence badged =
        getPackageManager().getUserBadgedLabel(getString(R.string.tailscale_profile), handle);
    return getString(R.string.other_user, badged, id);
  }

  private List<UserHandle> availableProfiles() {
    List<UserHandle> profiles =
        new ArrayList<>(getSystemService(UserManager.class).getUserProfiles());
    profiles.sort(
        Comparator.comparingInt(
            profile ->
                profile.hashCode() == currentUserId() ? Integer.MIN_VALUE : profile.hashCode()));
    return profiles;
  }

  private String userLabel(int id) {
    for (UserHandle profile : availableProfiles())
      if (profile.hashCode() == id) return userLabel(profile);
    return getString(R.string.android_user_id, id);
  }

  private static final class VpnCandidate {
    final int userId;
    final String ip;
    final String iface;

    VpnCandidate(int userId, String ip, String iface) {
      this.userId = userId;
      this.ip = ip;
      this.iface = iface;
    }
  }

  private List<VpnCandidate> vpnCandidates(String detail) {
    List<VpnCandidate> candidates = new ArrayList<>();
    String[] fields = detail.split("\\t", -1);
    for (int i = 1; i < fields.length; i++) {
      String[] values = fields[i].split(",", -1);
      if (values.length != 3
          || !values[0].matches("[0-9]{1,5}")
          || !values[1].matches(
              "100\\.(6[4-9]|[78][0-9]|9[0-9]|1[01][0-9]|12[0-7])\\.[0-9]{1,3}\\.[0-9]{1,3}")
          || !values[2].matches("[A-Za-z0-9_.-]{1,15}")) continue;
      try {
        candidates.add(new VpnCandidate(Integer.parseInt(values[0]), values[1], values[2]));
      } catch (NumberFormatException ignored) {
      }
    }
    return candidates;
  }

  private void selectTailscaleUser(int id) {
    var preferences = getSharedPreferences("settings", MODE_PRIVATE);
    if (preferences.getInt("tailscale_user", currentUserId()) == id) return;
    preferences.edit().putInt("tailscale_user", id).apply();
    String state = State.prefs(this).getString("state", "STOPPED");
    if (state.equals("LISTENING") || state.startsWith("WAITING_") || state.equals("RETRYING"))
      startService(new Intent(this, TailService.class).setAction("RESTART"));
    render();
  }

  private void renderVpnCandidates(String code, String detail) {
    List<VpnCandidate> candidates = vpnCandidates(detail);
    boolean visible = code.startsWith("WAITING_") && !candidates.isEmpty();
    vpnCandidatesCard.setVisibility(visible ? View.VISIBLE : View.GONE);
    if (!visible) return;
    int selected =
        getSharedPreferences("settings", MODE_PRIVATE).getInt("tailscale_user", currentUserId());
    vpnCandidatesTitle.setText(
        getResources()
            .getQuantityString(
                R.plurals.vpn_candidates_found, candidates.size(), candidates.size()));
    vpnCandidatesMessage.setText(
        code.equals("WAITING_MULTIPLE_TAILSCALE")
            ? R.string.vpn_candidates_same_user
            : R.string.vpn_candidates_choose);
    vpnCandidatesList.removeAllViews();
    for (VpnCandidate candidate : candidates) {
      MaterialCardView row = new MaterialCardView(this);
      LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(-1, -2);
      rowParams.topMargin = dp(8);
      row.setClickable(candidate.userId != selected);
      row.setFocusable(candidate.userId != selected);
      row.setStrokeWidth(dp(candidate.userId == selected ? 2 : 1));
      row.setStrokeColor(
          MaterialColors.getColor(
              row,
              candidate.userId == selected
                  ? androidx.appcompat.R.attr.colorPrimary
                  : com.google.android.material.R.attr.colorOutline));
      LinearLayout rowContent = new LinearLayout(this);
      rowContent.setOrientation(LinearLayout.HORIZONTAL);
      rowContent.setGravity(Gravity.CENTER_VERTICAL);
      rowContent.setPadding(dp(16), dp(10), dp(12), dp(10));
      ImageView icon = new ImageView(this);
      icon.setImageResource(R.drawable.ic_shield);
      icon.setColorFilter(MaterialColors.getColor(icon, androidx.appcompat.R.attr.colorPrimary));
      rowContent.addView(icon, new LinearLayout.LayoutParams(dp(32), dp(32)));
      LinearLayout labels = new LinearLayout(this);
      labels.setOrientation(LinearLayout.VERTICAL);
      labels.setPadding(dp(12), 0, dp(8), 0);
      MaterialTextView name = text(userLabel(candidate.userId), 16);
      name.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
      labels.addView(name);
      MaterialTextView network = text(candidate.ip + "  ·  " + candidate.iface, 14);
      network.setTypeface(Typeface.MONOSPACE);
      labels.addView(network);
      rowContent.addView(labels, new LinearLayout.LayoutParams(0, -2, 1));
      Chip choice = new Chip(this);
      choice.setText(
          candidate.userId == selected
              ? R.string.vpn_candidate_selected
              : R.string.vpn_candidate_use);
      choice.setCheckable(false);
      choice.setClickable(false);
      rowContent.addView(choice);
      row.addView(rowContent);
      if (candidate.userId != selected)
        row.setOnClickListener(v -> selectTailscaleUser(candidate.userId));
      vpnCandidatesList.addView(row, rowParams);
    }
  }

  private void showVpnUserDialog() {
    List<UserHandle> profiles = availableProfiles();
    List<VpnCandidate> detected = vpnCandidates(State.prefs(this).getString("detail", ""));
    String[] labels =
        profiles.stream()
            .map(
                profile -> {
                  String label = userLabel(profile);
                  for (VpnCandidate candidate : detected)
                    if (candidate.userId == profile.hashCode())
                      return label + "\n" + candidate.ip + "  ·  " + candidate.iface;
                  return label;
                })
            .toArray(String[]::new);
    int selected =
        getSharedPreferences("settings", MODE_PRIVATE).getInt("tailscale_user", currentUserId());
    int checked = 0;
    for (int i = 0; i < profiles.size(); i++)
      if (profiles.get(i).hashCode() == selected) checked = i;
    new MaterialAlertDialogBuilder(this)
        .setTitle(R.string.choose_tailscale_user)
        .setSingleChoiceItems(
            labels,
            checked,
            (dialog, index) -> {
              int id = profiles.get(index).hashCode();
              dialog.dismiss();
              selectTailscaleUser(id);
            })
        .setNegativeButton(android.R.string.cancel, null)
        .show();
  }

  private void render() {
    String code = State.prefs(this).getString("state", "STOPPED"),
        detail = State.prefs(this).getString("detail", "");
    boolean active = code.equals("LISTENING"),
        busy = code.equals("STARTING") || code.equals("RETRYING") || code.equals("STOPPING"),
        backend = active || code.startsWith("WAITING_") || code.equals("RETRYING");
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
    renderVpnCandidates(code, detail);
    String peer = State.prefs(this).getString("peer", "");
    var accessPreferences = getSharedPreferences("settings", MODE_PRIVATE);
    TreeSet<String> shellHosts = hostSet(accessPreferences.getString("shell_hosts", ""));
    TreeSet<String> rootHosts = hostSet(accessPreferences.getString("allowed_hosts", ""));
    boolean shellAllowed = shellHosts.contains(peer);
    boolean peerAllowed = rootHosts.contains(peer);
    peerState.setText(
        peer == null || peer.isBlank()
            ? "—"
            : peer
                + "  ·  "
                + getString(
                    peerAllowed
                        ? R.string.root_enabled
                        : shellAllowed ? R.string.shell_only : R.string.shell_pending));
    renderingAccess = true;
    shellToggle.setChecked(shellAllowed);
    rootToggle.setChecked(peerAllowed);
    renderingAccess = false;
    boolean havePeer = peer != null && !peer.isBlank();
    shellToggle.setEnabled(havePeer && !TailService.running);
    rootToggle.setEnabled(havePeer && !TailService.running);
    renderAuthorizedHosts(shellHosts, rootHosts);
    if (vpnUser != null) {
      int selected =
          getSharedPreferences("settings", MODE_PRIVATE).getInt("tailscale_user", currentUserId());
      vpnUser.setText(userLabel(selected));
      vpnUser.setEnabled(!TailService.running && !busy);
    }
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
