import java.util.Properties

plugins { id("com.android.application") }
val distribution=Properties().apply {
    providers.environmentVariable("TAILADB_SIGNING_PROPERTIES").orNull?.let { path ->
        file(path).inputStream().use { load(it) }
        for(key in listOf("storeFile","storePassword","keyAlias","keyPassword"))check(!getProperty(key).isNullOrBlank()){ "Missing release signing property: $key" }
    }
}
android {
    namespace = "net.ryuya_dev.tailadb"
    compileSdk = 36
    ndkVersion = "28.2.13676358"
    defaultConfig {
        applicationId = "net.ryuya_dev.tailadb"
        minSdk = 36
        targetSdk = 36
        versionCode = 3
        versionName = "0.2.1"
    }
    signingConfigs {
        if(!distribution.isEmpty)create("distribution") {
            storeFile=file(distribution.getProperty("storeFile"));storePassword=distribution.getProperty("storePassword")
            keyAlias=distribution.getProperty("keyAlias");keyPassword=distribution.getProperty("keyPassword")
        }
    }
    buildTypes { release { isDebuggable = false; isMinifyEnabled = false; signingConfig=signingConfigs.findByName("distribution") } }
    buildFeatures { buildConfig = true }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    packaging { jniLibs { useLegacyPackaging = true } }
    lint { abortOnError = true }
}
dependencies {
    implementation("com.google.android.material:material:1.14.0")
    testImplementation("junit:junit:4.13.2")
}
val nativeOutput = layout.buildDirectory.dir("generated/nativeLibs")
android.sourceSets.getByName("main").jniLibs.directories.add(nativeOutput.get().asFile.absolutePath)
val nativeSdk = androidComponents.sdkComponents.sdkDirectory
val compileNative = tasks.register<Exec>("compileTailAdbNative") {
    inputs.dir(rootProject.file("native"))
    inputs.file(rootProject.file("tools/build-native.sh"))
    outputs.dir(nativeOutput)
    workingDir = rootProject.projectDir
    doFirst {
        commandLine("bash", "tools/build-native.sh", nativeSdk.get().asFile.resolve("ndk/28.2.13676358").absolutePath, nativeOutput.get().asFile.absolutePath)
    }
}
tasks.named("preBuild").configure { dependsOn(compileNative) }
val exportRuntimeComponents = tasks.register("exportRuntimeComponents") {
    doLast {
        val items = configurations.getByName("releaseRuntimeClasspath").resolvedConfiguration.resolvedArtifacts.map { it.moduleVersion.id.toString() }.toSortedSet()
        val out = layout.buildDirectory.file("runtime-components.txt").get().asFile
        out.parentFile.mkdirs(); out.writeText(items.joinToString("\n")+"\n")
    }
}
val licenseOutput=layout.buildDirectory.dir("generated/licenseAssets")
android.sourceSets.getByName("main").assets.directories.add(licenseOutput.get().asFile.absolutePath)
val generateLicenses=tasks.register("generateLicenses") {
    dependsOn(exportRuntimeComponents)
    inputs.dir(rootProject.file("third_party")); inputs.file(rootProject.file("LICENSE"))
    inputs.file(layout.buildDirectory.file("runtime-components.txt")); outputs.dir(licenseOutput)
    doLast {
        val rows=rootProject.file("third_party/components.tsv").readLines().filter{it.isNotBlank()&&!it.startsWith("#")}.map{it.split('\t')}
        val actual=layout.buildDirectory.file("runtime-components.txt").get().asFile.readLines().filter{it.isNotBlank()}.toSet()
        check(actual==rows.map{it[0]}.toSet()){ "Review dependency licenses before building" }
        val text=buildString {
            append(rootProject.file("LICENSE").readText());append("\n\nThird-party runtime dependencies, including transitive dependencies\n\n")
            for(row in rows)append(row.joinToString("\n")+"\n\n")
            for(name in rows.map{it[2]}.distinct()){append("\n===== $name =====\n\n");append(rootProject.file("third_party/$name").readText())}
        }
        licenseOutput.get().asFile.mkdirs();licenseOutput.get().file("LICENSES.txt").asFile.writeText(text)
    }
}
tasks.named("preBuild").configure{dependsOn(generateLicenses)}
