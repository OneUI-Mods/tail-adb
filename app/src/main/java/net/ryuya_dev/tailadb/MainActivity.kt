package net.ryuya_dev.tailadb

import android.app.LocaleManager
import android.content.BroadcastReceiver
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Typeface
import android.os.Bundle
import android.os.LocaleList
import android.os.Process
import android.os.SystemClock
import android.os.UserHandle
import android.os.UserManager
import android.text.InputType
import android.view.Gravity
import android.view.MenuItem
import android.view.View
import android.widget.CompoundButton
import android.widget.CompoundButton.OnCheckedChangeListener
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.LinearLayout.LayoutParams
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.OnApplyWindowInsetsListener
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsCompat.Type
import androidx.core.widget.NestedScrollView
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.bottomnavigation.BottomNavigationView
import com.google.android.material.button.MaterialButton
import com.google.android.material.card.MaterialCardView
import com.google.android.material.chip.Chip
import com.google.android.material.color.DynamicColors
import com.google.android.material.color.MaterialColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.materialswitch.MaterialSwitch
import com.google.android.material.navigation.NavigationBarView.OnItemSelectedListener
import com.google.android.material.progressindicator.CircularProgressIndicator
import com.google.android.material.textfield.TextInputEditText
import com.google.android.material.textfield.TextInputLayout
import com.google.android.material.textview.MaterialTextView
import java.nio.charset.StandardCharsets
import java.util.ArrayList
import java.util.Comparator
import java.util.TreeSet
import java.util.concurrent.TimeUnit
import java.util.function.ToIntFunction
import kotlin.concurrent.Volatile
import kotlin.jvm.JvmField
import kotlin.math.round

class MainActivity : AppCompatActivity() {
    private lateinit var root: LinearLayout
    private var connection: LinearLayout? = null
    private var settings: LinearLayout? = null
    private var status: MaterialTextView? = null
    private var endpoint: MaterialTextView? = null
    private var rootState: MaterialTextView? = null
    private var peerState: MaterialTextView? = null
    private var networkUser: MaterialTextView? = null
    private var verifyNetwork: MaterialButton? = null
    private var badge: Chip? = null
    private var shield: ImageView? = null
    private var vpnCandidatesCard: MaterialCardView? = null
    private var vpnCandidatesTitle: MaterialTextView? = null
    private var vpnCandidatesMessage: MaterialTextView? = null
    private var vpnCandidatesList: LinearLayout? = null
    private var authorizedHostsList: LinearLayout? = null
    private var progress: CircularProgressIndicator? = null
    private var start: MaterialButton? = null
    private var stop: MaterialButton? = null
    private var copy: MaterialButton? = null
    private var companionCopy: MaterialButton? = null
    private var shellToggle: MaterialSwitch? = null
    private var rootToggle: MaterialSwitch? = null
    private var vpnUser: MaterialButton? = null
    private var portLayout: TextInputLayout? = null
    private var port: TextInputEditText? = null
    private var connectScroll: NestedScrollView? = null
    private var settingsScroll: NestedScrollView? = null
    private var registered: Boolean = false
    private var renderingAccess: Boolean = false
    private var statusRefresh: StatusRefreshLoop? = null
    private val receiver: BroadcastReceiver =
        object : BroadcastReceiver() {
            override fun onReceive(c: Context?, i: Intent?) {
                render()
                statusRefresh?.changed()
            }
        }

    override fun onCreate(saved: Bundle?) {
        DynamicColors.applyToActivityIfAvailable(this)
        super.onCreate(saved)
        root = LinearLayout(this)
        root!!.setOrientation(LinearLayout.VERTICAL)
        ViewCompat.setOnApplyWindowInsetsListener(
            root,
            OnApplyWindowInsetsListener { v: View?, insets: WindowInsetsCompat? ->
                val bars = insets!!.getInsets(Type.systemBars() or Type.displayCutout())
                v!!.setPadding(bars.left, bars.top, bars.right, bars.bottom)
                insets!!
            },
        )
        setContentView(root)
        val controller = WindowCompat.getInsetsController(getWindow(), root)
        val light: Boolean = (getResources().getConfiguration().uiMode and 0x30) != 0x20
        controller.setAppearanceLightStatusBars(light)
        controller.setAppearanceLightNavigationBars(light)
        val toolbar = MaterialToolbar(this)
        toolbar.setTitle(R.string.app_name)
        root!!.addView(toolbar, LayoutParams(-1, dp(64)))
        val content = FrameLayout(this)
        root!!.addView(content, LayoutParams(-1, 0, 1f))
        connectScroll = scroll()
        settingsScroll = scroll()
        connection = column()
        settings = column()
        connectScroll!!.addView(requireNotNull(connection))
        settingsScroll!!.addView(requireNotNull(settings))
        content.addView(connectScroll)
        content.addView(settingsScroll)
        settingsScroll!!.setVisibility(View.GONE)
        buildConnection()
        buildSettings()
        val nav = BottomNavigationView(this)
        nav.getMenu().add(0, 1, 0, R.string.connection).setIcon(R.drawable.ic_terminal)
        nav.getMenu().add(0, 2, 1, R.string.settings).setIcon(R.drawable.ic_shield)
        root!!.addView(nav, LayoutParams(-1, -2))
        nav.setOnItemSelectedListener(
            OnItemSelectedListener { item: MenuItem? ->
                connectScroll!!.setVisibility(
                    if (item!!.getItemId() == 1) View.VISIBLE else View.GONE
                )
                settingsScroll!!.setVisibility(
                    if (item!!.getItemId() == 2) View.VISIBLE else View.GONE
                )
                true
            }
        )
        render()
        if (!MainActivity.requestedRoot && !TailService.running) {
            MainActivity.requestedRoot = true
            requestRoot()
        }
    }

    private fun buildConnection() {
        val panel = card(connection!!)
        shield = ImageView(this)
        shield!!.setImageResource(R.drawable.ic_shield)
        shield!!.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO)
        panel.addView(shield, LayoutParams(dp(64), dp(64)))
        status = text("", 24)
        panel.addView(status)
        badge = Chip(this)
        badge!!.setClickable(false)
        badge!!.setCheckable(false)
        panel.addView(badge)
        progress = CircularProgressIndicator(this)
        progress!!.setIndeterminate(true)
        panel.addView(progress, LayoutParams(dp(32), dp(32)))
        panel.addView(text(getString(R.string.instructions), 16))
        networkUser = text("", 16)
        panel.addView(networkUser)
        verifyNetwork =
            button(
                R.string.verify_network,
                Runnable {
                    startService(Intent(this, TailService::class.java).setAction("VERIFY_NETWORK"))
                },
            )
        panel.addView(verifyNetwork, LayoutParams(-1, dp(56)))
        vpnCandidatesCard = MaterialCardView(this)
        val candidatesParams = LayoutParams(-1, -2)
        candidatesParams.bottomMargin = dp(16)
        connection!!.addView(vpnCandidatesCard, candidatesParams)
        val candidatesContent = column()
        vpnCandidatesCard!!.addView(candidatesContent)
        vpnCandidatesTitle = text("", 20)
        vpnCandidatesTitle!!.setTypeface(Typeface.DEFAULT, Typeface.BOLD)
        candidatesContent.addView(vpnCandidatesTitle)
        vpnCandidatesMessage = text("", 14)
        candidatesContent.addView(vpnCandidatesMessage)
        vpnCandidatesList = LinearLayout(this)
        vpnCandidatesList!!.setOrientation(LinearLayout.VERTICAL)
        candidatesContent.addView(vpnCandidatesList, LayoutParams(-1, -2))
        vpnCandidatesCard!!.setVisibility(View.GONE)
        val address = card(connection!!)
        address.addView(text(getString(R.string.endpoint), 16))
        endpoint = text("", 18)
        endpoint!!.setTypeface(Typeface.MONOSPACE)
        endpoint!!.setTextIsSelectable(true)
        address.addView(endpoint)
        copy =
            button(
                R.string.copy,
                Runnable {
                    val detail = State.prefs(this)!!.getString("detail", "")
                    val value: String? = detail!!.split("\t".toRegex()).toTypedArray()[0]
                    if (!TextCompatibility.isBlank(value!!)) {
                        getSystemService<ClipboardManager?>(ClipboardManager::class.java)
                            .setPrimaryClip(ClipData.newPlainText("ADB", "adb connect " + value))
                        Toast.makeText(this, R.string.copied, Toast.LENGTH_SHORT).show()
                    }
                },
            )
        address.addView(copy)
        address.addView(text(getString(R.string.companion_note), 14))
        companionCopy =
            button(
                R.string.companion_copy,
                Runnable {
                    val endpoint: String? =
                        State.prefs(this)!!.getString("detail", "")!!.split("\t".toRegex())
                            .toTypedArray()[0]
                    if (endpoint!!.matches("100\\.[0-9.]+:[0-9]{4,5}".toRegex())) {
                        getSystemService<ClipboardManager?>(ClipboardManager::class.java)
                            .setPrimaryClip(
                                ClipData.newPlainText(
                                    "Tail ADB",
                                    "python3 tailadb_watch.py --device " + endpoint,
                                )
                            )
                        Toast.makeText(this, R.string.copied, Toast.LENGTH_SHORT).show()
                    }
                },
            )
        address.addView(companionCopy)
        val access = card(connection!!)
        access.addView(text(getString(R.string.connected_host), 16))
        peerState = text("—", 18)
        peerState!!.setTypeface(Typeface.MONOSPACE)
        access.addView(peerState)
        shellToggle = MaterialSwitch(this)
        shellToggle!!.setText(R.string.shell_access)
        shellToggle!!.setOnCheckedChangeListener(
            OnCheckedChangeListener { button: CompoundButton?, checked: Boolean ->
                if (!renderingAccess) setCurrentPeerAccess("shell_hosts", checked)
            }
        )
        access.addView(shellToggle, LayoutParams(-1, dp(56)))
        rootToggle = MaterialSwitch(this)
        rootToggle!!.setText(R.string.root_access)
        rootToggle!!.setOnCheckedChangeListener(
            OnCheckedChangeListener { button: CompoundButton?, checked: Boolean ->
                if (!renderingAccess) setCurrentPeerAccess("allowed_hosts", checked)
            }
        )
        access.addView(rootToggle, LayoutParams(-1, dp(56)))
        portLayout = TextInputLayout(this)
        portLayout!!.setHint(getString(R.string.port))
        portLayout!!.setBoxBackgroundMode(TextInputLayout.BOX_BACKGROUND_OUTLINE)
        port = TextInputEditText(portLayout!!.getContext())
        port!!.setInputType(InputType.TYPE_CLASS_NUMBER)
        port!!.setText(
            getSharedPreferences("settings", Context.MODE_PRIVATE).getInt("port", 5555).toString()
        )
        portLayout!!.addView(port)
        connection!!.addView(portLayout, LayoutParams(-1, -2))
        start =
            button(
                R.string.start,
                Runnable {
                    val number: Int
                    try {
                        number = port!!.getText().toString().toInt()
                        if (number < 1024 || number > 65535) throw NumberFormatException()
                    } catch (e: NumberFormatException) {
                        portLayout!!.setError(getString(R.string.port_error))
                        return@Runnable
                    }
                    portLayout!!.setError(null)
                    getSharedPreferences("settings", Context.MODE_PRIVATE)
                        .edit()
                        .putInt("port", number)
                        .apply()
                    startService(Intent(this, TailService::class.java))
                    render()
                },
            )
        connection!!.addView(start, LayoutParams(-1, dp(56)))
        stop =
            button(
                R.string.stop,
                Runnable { startService(Intent(this, TailService::class.java).setAction("STOP")) },
            )
        connection!!.addView(stop, LayoutParams(-1, dp(56)))
    }

    private fun buildSettings() {
        val security = card(settings!!)
        security.addView(text(getString(R.string.scope), 16))
        security.addView(text(getString(R.string.compatibility), 16))
        security.addView(text(getString(R.string.isolation), 16))
        security.addView(text(getString(R.string.root_host_explanation), 16))
        security.addView(text(getString(R.string.authorized_hosts), 20))
        security.addView(text(getString(R.string.authorized_hosts_explanation), 14))
        authorizedHostsList = LinearLayout(this)
        authorizedHostsList!!.setOrientation(LinearLayout.VERTICAL)
        security.addView(authorizedHostsList, LayoutParams(-1, -2))
        val startup = card(settings!!)
        val vpn = card(settings!!)
        vpn.addView(text(getString(R.string.tailscale_user), 16))
        vpnUser = button(R.string.choose_tailscale_user, Runnable { this.showVpnUserDialog() })
        vpn.addView(vpnUser, LayoutParams(-1, dp(56)))
        vpn.addView(text(getString(R.string.tailscale_user_explanation), 14))
        val autoStart = MaterialSwitch(this)
        autoStart.setText(R.string.auto_start)
        autoStart.setChecked(BootReceiver.isEnabled(this))
        autoStart.setOnCheckedChangeListener(
            OnCheckedChangeListener { button: CompoundButton?, checked: Boolean ->
                BootReceiver.setEnabled(this, checked)
            }
        )
        startup.addView(autoStart, LayoutParams(-1, dp(56)))
        startup.addView(text(getString(R.string.auto_start_explanation), 14))
        val permissions = card(settings!!)
        permissions.addView(text(getString(R.string.root_hint), 16))
        rootState = text(getString(R.string.root_checking), 16)
        permissions.addView(rootState)
        settings!!.addView(
            button(
                R.string.language,
                Runnable {
                    val labels =
                        arrayOf<String?>(getString(R.string.system_language), "English", "日本語")
                    val tags = arrayOf<String?>("", "en", "ja")
                    MaterialAlertDialogBuilder(this)
                        .setTitle(R.string.choose_language)
                        .setItems(
                            labels,
                            android.content.DialogInterface.OnClickListener {
                                d: DialogInterface?,
                                n: Int ->
                                getSystemService<LocaleManager?>(LocaleManager::class.java)
                                    .setApplicationLocales(LocaleList.forLanguageTags(tags[n]))
                            },
                        )
                        .show()
                },
            ),
            LayoutParams(-1, dp(56)),
        )
        settings!!.addView(
            button(
                R.string.licenses,
                Runnable { startActivity(Intent(this, LicensesActivity::class.java)) },
            ),
            LayoutParams(-1, dp(56)),
        )
        settings!!.addView(text(getString(R.string.version, BuildConfig.VERSION_NAME), 14))
    }

    private fun requestRoot() {
        Thread(
                Runnable {
                    var allowed: Boolean = false
                    try {
                        val p = ProcessBuilder("su", "-c", "id").redirectErrorStream(true).start()
                        if (p.waitFor(30, TimeUnit.SECONDS))
                            allowed =
                                p.exitValue() == 0 &&
                                    String(
                                            p.getInputStream().readAllBytes(),
                                            StandardCharsets.UTF_8,
                                        )
                                        .contains("uid=0(")
                        else p.destroy()
                    } catch (ignored: Exception) {}
                    val result: Boolean = allowed
                    MainActivity.rootAvailable = result
                    runOnUiThread(
                        Runnable {
                            if (!isDestroyed()) {
                                rootState!!.setText(
                                    if (result) R.string.root_allowed else R.string.state_root
                                )
                                if (result) statusRefresh?.changed()
                                if (!result && !TailService.running)
                                    State.set(this, "ERROR_ROOT", "")
                                render()
                            }
                        }
                    )
                },
                "TailAdbRootCheck",
            )
            .start()
    }

    private fun setCurrentPeerAccess(preference: String, allowed: Boolean) {
        val peer = State.prefs(this)!!.getString("peer", "")
        if (peer == null || TextCompatibility.isBlank(peer)) return
        setHostAccess(peer, preference, allowed)
    }

    private fun setHostAccess(peer: String?, preference: String, allowed: Boolean) {
        val preferences = getSharedPreferences("settings", Context.MODE_PRIVATE)
        val saved = preferences.getString(preference, "")
        val hosts = TreeSet<String?>()
        if (saved != null)
            for (item in TextCompatibility.split(saved, ",")) if (!TextCompatibility.isBlank(item))
                hosts.add(item)
        if (allowed) hosts.add(peer) else hosts.remove(peer)
        val edit = preferences.edit().putString(preference, hosts.joinToString(","))
        if (preference == "allowed_hosts" && allowed) {
            val shell = hostSet(preferences.getString("shell_hosts", ""))
            shell.add(peer)
            edit.putString("shell_hosts", shell.joinToString(","))
        } else if (preference == "shell_hosts" && !allowed) {
            val root = hostSet(preferences.getString("allowed_hosts", ""))
            root.remove(peer)
            edit.putString("allowed_hosts", root.joinToString(","))
        }
        edit.apply()
        val state = State.prefs(this)!!.getString("state", "STOPPED")
        if (state != "STOPPED" && !state!!.startsWith("ERROR_"))
            startService(Intent(this, TailService::class.java).setAction("RESTART"))
        render()
    }

    private fun renderAuthorizedHosts(shell: TreeSet<String?>, root: TreeSet<String?>) {
        authorizedHostsList!!.removeAllViews()
        val all = TreeSet<String?>(shell)
        all.addAll(root)
        if (all.isEmpty()) {
            authorizedHostsList!!.addView(text(getString(R.string.no_authorized_hosts), 16))
            return
        }
        for (host in all) {
            val row = MaterialCardView(this)
            val rowParams = LayoutParams(-1, -2)
            rowParams.topMargin = dp(8)
            val content = column()
            val address = text(host, 17)
            address.setTypeface(Typeface.MONOSPACE, Typeface.BOLD)
            content.addView(address)
            val shellAccess = MaterialSwitch(this)
            shellAccess.setText(R.string.shell_access)
            shellAccess.setChecked(shell.contains(host))
            shellAccess.setOnCheckedChangeListener(
                OnCheckedChangeListener { button: CompoundButton?, checked: Boolean ->
                    setHostAccess(host, "shell_hosts", checked)
                }
            )
            content.addView(shellAccess, LayoutParams(-1, dp(52)))
            val rootAccess = MaterialSwitch(this)
            rootAccess.setText(R.string.root_access)
            rootAccess.setChecked(root.contains(host))
            rootAccess.setOnCheckedChangeListener(
                OnCheckedChangeListener { button: CompoundButton?, checked: Boolean ->
                    setHostAccess(host, "allowed_hosts", checked)
                }
            )
            content.addView(rootAccess, LayoutParams(-1, dp(52)))
            row.addView(content)
            authorizedHostsList!!.addView(row, rowParams)
        }
    }

    private fun hostSet(saved: String?): TreeSet<String?> {
        val hosts = TreeSet<String?>()
        if (saved != null)
            for (item in TextCompatibility.split(saved, ",")) if (!TextCompatibility.isBlank(item))
                hosts.add(item)
        return hosts
    }

    private fun currentUserId(): Int {
        return Process.myUid() / 100000
    }

    private fun userLabel(handle: UserHandle): String? {
        val id = handle.hashCode()
        if (id == currentUserId()) return getString(R.string.current_user, id)
        val badged =
            getPackageManager().getUserBadgedLabel(getString(R.string.tailscale_profile), handle)
        return getString(R.string.other_user, badged, id)
    }

    private fun availableProfiles(): MutableList<UserHandle?> {
        val profiles =
            ArrayList<UserHandle?>(
                getSystemService<UserManager?>(UserManager::class.java).getUserProfiles()
            )
        profiles.sortWith(
            Comparator.comparingInt<UserHandle?>(
                ToIntFunction { profile: UserHandle? ->
                    if (profile.hashCode() == currentUserId()) Int.MIN_VALUE else profile.hashCode()
                }
            )
        )
        return profiles
    }

    private fun userLabel(id: Int): String? {
        for (profile in availableProfiles()) if (profile.hashCode() == id)
            return userLabel(profile!!)
        return getString(R.string.android_user_id, id)
    }

    private class VpnCandidate(userId: Int, ip: String, iface: String) {
        @JvmField val userId: Int
        @JvmField val ip: String
        @JvmField val iface: String

        init {
            this.userId = userId
            this.ip = ip
            this.iface = iface
        }
    }

    private fun vpnCandidates(detail: String): MutableList<MainActivity.VpnCandidate?> {
        val candidates = ArrayList<MainActivity.VpnCandidate?>()
        val fields = detail.split("\\t".toRegex()).toTypedArray()
        for (i in 1 until fields.size) {
            val values = fields[i].split(",".toRegex()).toTypedArray()
            if (
                values.size != 3 ||
                    !values[0].matches("[0-9]{1,5}".toRegex()) ||
                    !values[1].matches(
                        "100\\.(6[4-9]|[78][0-9]|9[0-9]|1[01][0-9]|12[0-7])\\.[0-9]{1,3}\\.[0-9]{1,3}"
                            .toRegex()
                    ) ||
                    !values[2].matches("[A-Za-z0-9_.-]{1,15}".toRegex())
            )
                continue
            try {
                candidates.add(MainActivity.VpnCandidate(values[0].toInt(), values[1], values[2]))
            } catch (ignored: NumberFormatException) {}
        }
        return candidates
    }

    private fun selectTailscaleUser(id: Int) {
        val preferences = getSharedPreferences("settings", Context.MODE_PRIVATE)
        if (preferences.getInt("tailscale_user", currentUserId()) == id) return
        preferences.edit().putInt("tailscale_user", id).apply()
        val state = State.prefs(this)!!.getString("state", "STOPPED")
        if (state == "LISTENING" || state!!.startsWith("WAITING_") || state == "RETRYING")
            startService(Intent(this, TailService::class.java).setAction("RESTART"))
        render()
    }

    private fun renderVpnCandidates(code: String, detail: String) {
        val candidates = vpnCandidates(detail)
        val visible: Boolean = code.startsWith("WAITING_") && !candidates.isEmpty()
        vpnCandidatesCard!!.setVisibility(if (visible) View.VISIBLE else View.GONE)
        if (!visible) return
        val selected =
            getSharedPreferences("settings", Context.MODE_PRIVATE)
                .getInt("tailscale_user", currentUserId())
        vpnCandidatesTitle!!.setText(
            getResources()
                .getQuantityString(
                    R.plurals.vpn_candidates_found,
                    candidates.size,
                    candidates.size,
                )
        )
        vpnCandidatesMessage!!.setText(
            if (code == "WAITING_MULTIPLE_TAILSCALE") R.string.vpn_candidates_same_user
            else R.string.vpn_candidates_choose
        )
        vpnCandidatesList!!.removeAllViews()
        for (candidate in candidates) {
            val row = MaterialCardView(this)
            val rowParams = LayoutParams(-1, -2)
            rowParams.topMargin = dp(8)
            row.setClickable(candidate!!.userId != selected)
            row.setFocusable(candidate!!.userId != selected)
            row.setStrokeWidth(dp(if (candidate!!.userId == selected) 2 else 1))
            row.setStrokeColor(
                MaterialColors.getColor(
                    row,
                    if (candidate!!.userId == selected) androidx.appcompat.R.attr.colorPrimary
                    else com.google.android.material.R.attr.colorOutline,
                )
            )
            val rowContent = LinearLayout(this)
            rowContent.setOrientation(LinearLayout.HORIZONTAL)
            rowContent.setGravity(Gravity.CENTER_VERTICAL)
            rowContent.setPadding(dp(16), dp(10), dp(12), dp(10))
            val icon = ImageView(this)
            icon.setImageResource(R.drawable.ic_shield)
            icon.setColorFilter(
                MaterialColors.getColor(icon, androidx.appcompat.R.attr.colorPrimary)
            )
            rowContent.addView(icon, LayoutParams(dp(32), dp(32)))
            val labels = LinearLayout(this)
            labels.setOrientation(LinearLayout.VERTICAL)
            labels.setPadding(dp(12), 0, dp(8), 0)
            val name = text(userLabel(candidate!!.userId), 16)
            name.setTypeface(Typeface.DEFAULT, Typeface.BOLD)
            labels.addView(name)
            val network = text(candidate!!.ip + "  ·  " + candidate!!.iface, 14)
            network.setTypeface(Typeface.MONOSPACE)
            labels.addView(network)
            rowContent.addView(labels, LayoutParams(0, -2, 1f))
            val choice = Chip(this)
            choice.setText(
                if (candidate!!.userId == selected) R.string.vpn_candidate_selected
                else R.string.vpn_candidate_use
            )
            choice.setCheckable(false)
            choice.setClickable(false)
            rowContent.addView(choice)
            row.addView(rowContent)
            if (candidate!!.userId != selected)
                row.setOnClickListener(
                    android.view.View.OnClickListener { v: View? ->
                        selectTailscaleUser(candidate!!.userId)
                    }
                )
            vpnCandidatesList!!.addView(row, rowParams)
        }
    }

    private fun showVpnUserDialog() {
        val profiles = availableProfiles()
        val detected = vpnCandidates(State.prefs(this)!!.getString("detail", "")!!)
        val labels =
            profiles
                .stream()
                .map<String?> { profile: UserHandle? ->
                    val label = userLabel(profile!!)
                    for (candidate in detected) if (candidate!!.userId == profile.hashCode())
                        return@map label + "\n" + candidate!!.ip + "  ·  " + candidate!!.iface
                    label
                }
                .toArray { size -> arrayOfNulls<String>(size) }
        val selected =
            getSharedPreferences("settings", Context.MODE_PRIVATE)
                .getInt("tailscale_user", currentUserId())
        var checked: Int = 0
        for (i in profiles.indices) if (profiles.get(i).hashCode() == selected) checked = i
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.choose_tailscale_user)
            .setSingleChoiceItems(
                labels,
                checked,
                android.content.DialogInterface.OnClickListener {
                    dialog: DialogInterface?,
                    index: Int ->
                    val id = profiles.get(index).hashCode()
                    dialog!!.dismiss()
                    selectTailscaleUser(id)
                },
            )
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun render() {
        val code = State.prefs(this)!!.getString("state", "STOPPED")
        val detail = State.prefs(this)!!.getString("detail", "")
        val active: Boolean = code == "LISTENING"
        val busy: Boolean = code == "STARTING" || code == "RETRYING" || code == "STOPPING"
        val backend: Boolean = active || code!!.startsWith("WAITING_") || code == "RETRYING"
        status!!.setText(R.string.connection)
        badge!!.setText(State.label(code))
        val icon: Int =
            if (active) R.drawable.ic_shield
            else (if (code!!.startsWith("ERROR_")) R.drawable.ic_error else R.drawable.ic_clock)
        shield!!.setImageResource(icon)
        badge!!.setChipIconResource(icon)
        if (active) rootState!!.setText(R.string.root_allowed)
        val color =
            MaterialColors.getColor(
                requireNotNull(shield),
                if (code!!.startsWith("ERROR_")) android.R.attr.colorError
                else androidx.appcompat.R.attr.colorPrimary,
            )
        shield!!.setColorFilter(color)
        progress!!.setVisibility(if (busy) View.VISIBLE else View.GONE)
        val selectedUser =
            getSharedPreferences("settings", Context.MODE_PRIVATE)
                .getInt("tailscale_user", currentUserId())
        val checkAge: Long =
            SystemClock.elapsedRealtime() - State.prefs(this)!!.getLong("network_time", 0)
        val checked: Boolean =
            State.prefs(this)!!.getInt("network_user", -1) == selectedUser &&
                checkAge >= 0 &&
                checkAge < 45000
        var result: String? =
            if (checked) State.prefs(this)!!.getString("network_state", "") else ""
        val vpnFields = detail!!.split("\t".toRegex()).toTypedArray()
        val selectedMatches: Boolean =
            !active || (vpnFields.size >= 3 && selectedUser.toString() == vpnFields[2])
        if (!checked && active && vpnFields.size >= 3 && selectedUser.toString() == vpnFields[2])
            result = "READY"
        if (!selectedMatches) result = "MISMATCH"
        else if (code == "WAITING_SELECTED_USER") result = "OTHER_USER"
        else if (code == "WAITING_TAILSCALE") result = "UNAVAILABLE"
        else if (code == "WAITING_NETWORK_INSPECTION") result = "INSPECTION_ERROR"
        val checkLabel: Int =
            when (result) {
                "READY" -> R.string.network_verified
                "OTHER_USER" -> R.string.network_other_user
                "MISMATCH" -> R.string.network_mismatch
                "MULTIPLE" -> R.string.state_multiple_tailscale
                "UNAVAILABLE" -> R.string.network_not_connected
                "INSPECTION_ERROR" -> R.string.state_network_inspection
                else -> R.string.network_unverified
            }
        networkUser!!.setText(
            getString(R.string.network_user_status, userLabel(selectedUser), getString(checkLabel))
        )
        verifyNetwork!!.setEnabled(!TailService.running)
        val host: String? = detail!!.split("\t".toRegex()).toTypedArray()[0]
        endpoint!!.setText(if (active) "adb connect " + host else "—")
        copy!!.setEnabled(active && selectedMatches)
        val endpointFields = detail!!.split("\t".toRegex()).toTypedArray()
        companionCopy!!.setEnabled(
            active &&
                selectedMatches &&
                endpointFields.size >= 5 &&
                endpointFields[4].matches("[1-9][0-9]{3,4}".toRegex())
        )
        start!!.setEnabled(!TailService.running && !busy && !backend)
        stop!!.setEnabled(!TailService.running && backend)
        port!!.setEnabled(!TailService.running && !backend)
        renderVpnCandidates(code!!, detail!!)
        val peer = State.prefs(this)!!.getString("peer", "")
        val accessPreferences = getSharedPreferences("settings", Context.MODE_PRIVATE)
        val shellHosts = hostSet(accessPreferences.getString("shell_hosts", ""))
        val rootHosts = hostSet(accessPreferences.getString("allowed_hosts", ""))
        val shellAllowed = shellHosts.contains(peer)
        val peerAllowed = rootHosts.contains(peer)
        peerState!!.setText(
            if (peer == null || TextCompatibility.isBlank(peer)) "—"
            else
                (peer +
                    "  ·  " +
                    getString(
                        if (peerAllowed) R.string.root_enabled
                        else if (shellAllowed) R.string.shell_only else R.string.shell_pending
                    ))
        )
        renderingAccess = true
        shellToggle!!.setChecked(shellAllowed)
        rootToggle!!.setChecked(peerAllowed)
        renderingAccess = false
        val havePeer: Boolean = peer != null && !TextCompatibility.isBlank(peer)
        shellToggle!!.setEnabled(havePeer && !TailService.running)
        rootToggle!!.setEnabled(havePeer && !TailService.running)
        renderAuthorizedHosts(shellHosts, rootHosts)
        if (vpnUser != null) {
            val selected =
                getSharedPreferences("settings", Context.MODE_PRIVATE)
                    .getInt("tailscale_user", currentUserId())
            vpnUser!!.setText(userLabel(selected))
            vpnUser!!.setEnabled(!TailService.running && !busy)
        }
        badge!!.setOnClickListener(
            android.view.View.OnClickListener { v: View? ->
                MaterialAlertDialogBuilder(this)
                    .setTitle(R.string.technical)
                    .setMessage(
                        code + (if (TextCompatibility.isBlank(detail!!)) "" else "\n" + detail)
                    )
                    .setPositiveButton(android.R.string.ok, null)
                    .show()
            }
        )
    }

    override fun onStart() {
        super.onStart()
        registerReceiver(receiver, IntentFilter(State.ACTION), Context.RECEIVER_NOT_EXPORTED)
        registered = true
        statusRefresh =
            StatusRefreshLoop(
                snapshot = {
                    val state = State.prefs(this)!!
                    listOf(
                        state.getString("state", null),
                        state.getString("detail", null),
                        state.getString("peer", null),
                    )
                },
                refresh = {
                    if (MainActivity.rootAvailable)
                        TailService.refreshState(getApplicationContext())
                    runOnUiThread {
                        if (statusRefresh != null && !isFinishing) render()
                    }
                },
            )
        statusRefresh!!.changed()
        render()
    }

    override fun onStop() {
        if (registered) {
            unregisterReceiver(receiver)
            registered = false
        }
        if (statusRefresh != null) {
            statusRefresh!!.close()
            statusRefresh = null
        }
        super.onStop()
    }

    private fun dp(value: Int): Int {
        return Math.round(value * getResources().getDisplayMetrics().density)
    }

    private fun column(): LinearLayout {
        val box = LinearLayout(this)
        box.setOrientation(LinearLayout.VERTICAL)
        box.setPadding(dp(20), dp(12), dp(20), dp(24))
        return box
    }

    private fun scroll(): NestedScrollView {
        val view = NestedScrollView(this)
        view.setFillViewport(true)
        return view
    }

    private fun card(parent: LinearLayout): LinearLayout {
        val card = MaterialCardView(this)
        val lp = LayoutParams(-1, -2)
        lp.bottomMargin = dp(16)
        parent.addView(card, lp)
        val inside = column()
        card.addView(inside)
        return inside
    }

    private fun text(value: String?, size: Int): MaterialTextView {
        val t = MaterialTextView(this)
        t.setText(value)
        t.setTextSize(size.toFloat())
        t.setPadding(0, dp(6), 0, dp(10))
        return t
    }

    private fun button(label: Int, action: Runnable): MaterialButton {
        val b = MaterialButton(this)
        b.setText(label)
        b.setOnClickListener(android.view.View.OnClickListener { v: View? -> action.run() })
        return b
    }

    companion object {
        private var requestedRoot: Boolean = false
        @Volatile private var rootAvailable: Boolean = false
    }
}
