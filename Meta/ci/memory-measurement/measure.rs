// Temporary (issue #28): how much memory one WebContent process spends on the content blocker for Xenon's filter lists.
//
// Each scenario runs in a fresh child process, which reads the lists as Application::load_content_blocker_lists() joins
// them, builds the engine with the same entry point WebContent uses (rust_content_blocker_create), frees its copy of
// the text (WebContent's is a shared buffer, unmapped after the IPC call), and reports its memory: physical footprint
// on macOS (Activity Monitor's Memory column), RSS on Linux.
//
//   measure <filter-lists directory>

use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

#[global_allocator]
static GLOBAL: mimalloc::MiMalloc = mimalloc::MiMalloc;

unsafe extern "C" {
    fn mi_collect(force: bool);
}

// The C++ side of RustPanic.rs and RustDemangle.rs, which the content blocker crate includes.
#[unsafe(no_mangle)]
extern "C" fn ladybird_rust_panic(
    _: *const u8,
    _: usize,
    _: *const u8,
    _: usize,
    _: u32,
    _: u32,
    _: extern "C" fn() -> bool,
) {
}
#[unsafe(no_mangle)]
extern "C" fn ladybird_rust_panic_hook_returned() {}
#[unsafe(no_mangle)]
extern "C" fn ladybird_rust_panic_may_abort() {}
#[unsafe(no_mangle)]
extern "C" fn ladybird_rust_panic_will_abort() {}
#[unsafe(no_mangle)]
extern "C" fn ladybird_set_rust_symbol_demangler(_: *const u8) {}

#[derive(Clone, Copy)]
struct Memory {
    current: u64,
    peak: u64,
}

#[cfg(target_os = "macos")]
fn memory() -> Memory {
    let mut info: libc::rusage_info_v4 = unsafe { std::mem::zeroed() };
    let result = unsafe {
        libc::proc_pid_rusage(
            std::process::id() as i32,
            libc::RUSAGE_INFO_V4,
            (&raw mut info).cast::<libc::rusage_info_t>(),
        )
    };
    assert_eq!(result, 0);
    Memory {
        current: info.ri_phys_footprint,
        peak: info.ri_lifetime_max_phys_footprint,
    }
}

#[cfg(target_os = "linux")]
fn memory() -> Memory {
    let status = std::fs::read_to_string("/proc/self/status").unwrap();
    let field = |name: &str| -> u64 {
        status
            .lines()
            .find_map(|line| line.strip_prefix(name))
            .map(|value| value.trim().trim_end_matches("kB").trim().parse::<u64>().unwrap() * 1024)
            .unwrap_or(0)
    };
    Memory {
        current: field("VmRSS:"),
        peak: field("VmHWM:"),
    }
}

fn collect() {
    unsafe { mi_collect(true) };
}

fn mib(bytes: u64) -> String {
    format!("{:.1}", bytes as f64 / (1024.0 * 1024.0))
}

fn signed_mib(after: u64, before: u64) -> String {
    if after >= before {
        mib(after - before)
    } else {
        format!("-{}", mib(before - after))
    }
}

struct Scenario {
    name: String,
    lists: Vec<PathBuf>,
    trusted_lists: Vec<PathBuf>,
    resources: Option<PathBuf>,
}

fn text_files(directory: &Path) -> Vec<PathBuf> {
    let Ok(entries) = std::fs::read_dir(directory) else {
        return Vec::new();
    };
    let mut paths: Vec<_> = entries
        .map(|entry| entry.unwrap().path())
        .filter(|path| path.extension().is_some_and(|extension| extension == "txt"))
        .collect();
    paths.sort();
    paths
}

fn scenarios(directory: &Path) -> Vec<Scenario> {
    let lists = text_files(&directory.join("lists"));
    let trusted_lists = text_files(&directory.join("trusted-lists"));
    let resources = directory.join("resources/ublock-resources.json");
    let resources = resources.exists().then_some(resources);

    let mut scenarios = vec![
        Scenario {
            name: "Xenon: all bundled lists and resources".into(),
            lists: lists.clone(),
            trusted_lists: trusted_lists.clone(),
            resources: resources.clone(),
        },
        Scenario {
            name: "Xenon without urlhaus-1".into(),
            lists: lists.iter().filter(|path| !path.ends_with("urlhaus-1.txt")).cloned().collect(),
            trusted_lists: trusted_lists.clone(),
            resources: resources.clone(),
        },
        Scenario {
            name: "EasyList + EasyPrivacy only".into(),
            lists: lists
                .iter()
                .filter(|path| path.ends_with("easylist.txt") || path.ends_with("easyprivacy.txt"))
                .cloned()
                .collect(),
            trusted_lists: Vec::new(),
            resources: None,
        },
    ];
    for list in &lists {
        scenarios.push(Scenario {
            name: format!("lists/{}", list.file_name().unwrap().to_string_lossy()),
            lists: vec![list.clone()],
            trusted_lists: Vec::new(),
            resources: None,
        });
    }
    for list in &trusted_lists {
        scenarios.push(Scenario {
            name: format!("trusted-lists/{}", list.file_name().unwrap().to_string_lossy()),
            lists: Vec::new(),
            trusted_lists: vec![list.clone()],
            resources: resources.clone(),
        });
    }
    scenarios
}

// As Application::load_content_blocker_lists() joins them: each file followed by a newline, then the custom filters
// (none here) and a newline.
fn joined(paths: &[PathBuf], with_custom_filters: bool) -> Vec<u8> {
    let mut bytes = Vec::new();
    for path in paths {
        bytes.extend(std::fs::read(path).unwrap());
        bytes.push(b'\n');
    }
    if with_custom_filters {
        bytes.push(b'\n');
    }
    bytes
}

// Builds the engine WebContent builds, and prints one tab-separated row.
fn measure_webcontent(scenario: &Scenario) {
    collect();
    let start = memory();

    let rules = joined(&scenario.lists, true);
    let trusted_rules = joined(&scenario.trusted_lists, false);
    let resources = scenario.resources.as_ref().map(std::fs::read).transpose().unwrap().unwrap_or_default();
    let text_bytes = (rules.len() + trusted_rules.len() + resources.len()) as u64;
    let with_text = memory();

    let timer = Instant::now();
    let engine = libweb_content_blocker_rust::rust_content_blocker_create(
        ak::Span::new(&rules),
        ak::Span::new(&trusted_rules),
        ak::Span::new(&resources),
    );
    let build_time = timer.elapsed();
    assert!(!engine.is_null());
    let built = memory();

    drop((rules, trusted_rules, resources));
    collect();
    let retained = memory();

    println!(
        "{}\twebcontent\t{}\t{}\t{}\t{}\t{}",
        scenario.name,
        mib(text_bytes),
        build_time.as_millis(),
        signed_mib(built.peak, with_text.current),
        signed_mib(retained.current, start.current),
        "",
    );
    std::hint::black_box(engine);
}

// The plain adblock engine for the same lists (what main_engine_from_lists builds, without Xenon's popup and CNAME
// engines or the generic selector list index), and its serialized size, which is what it keeps in memory.
fn measure_adblock(scenario: &Scenario, serialized_path: Option<&Path>) {
    collect();
    let start = memory();

    let rules = String::from_utf8(joined(&scenario.lists, true)).unwrap();
    let trusted_rules = String::from_utf8(joined(&scenario.trusted_lists, false)).unwrap();
    let with_text = memory();

    let timer = Instant::now();
    let mut filter_set = adblock::lists::FilterSet::new(false);
    filter_set.add_filter_list(rules, adblock::lists::ParseOptions::default());
    filter_set.add_filter_list(
        trusted_rules,
        adblock::lists::ParseOptions {
            permissions: adblock::resources::PermissionMask::from_bits(0x01),
            ..Default::default()
        },
    );
    let engine = adblock::engine::Engine::new_with_filter_set(filter_set);
    let build_time = timer.elapsed();
    let built = memory();

    collect();
    let retained = memory();

    let serialized = engine.serialize();
    if let Some(path) = serialized_path {
        std::fs::write(path, &serialized).unwrap();
    }
    println!(
        "{}\tadblock only\t\t{}\t{}\t{}\t{}",
        scenario.name,
        build_time.as_millis(),
        signed_mib(built.peak, with_text.current),
        signed_mib(retained.current, start.current),
        mib(serialized.len() as u64),
    );
    std::hint::black_box(engine);
}

// Loads a serialized engine, as a process would if it were handed one instead of the lists.
fn measure_deserialize(path: &Path) {
    let serialized = std::fs::read(path).unwrap();
    collect();
    let start = memory();

    let timer = Instant::now();
    let mut engine = adblock::engine::Engine::default();
    engine.deserialize(&serialized).unwrap();
    let load_time = timer.elapsed();
    let loaded = memory();

    collect();
    let retained = memory();
    println!(
        "Xenon: all bundled lists\tdeserialized\t{}\t{}\t{}\t{}\t{}",
        mib(serialized.len() as u64),
        load_time.as_millis(),
        signed_mib(loaded.peak, start.current),
        signed_mib(retained.current, start.current),
        "",
    );
    std::hint::black_box(engine);
}

// Checks that the engine WebContent builds blocks the same pages as a plain adblock engine holding every filter, for
// URLs made from the lists' `||host/path^$document` filters, which Xenon's patch keeps out of the engine.
fn verify(scenario: &Scenario) {
    let rules = joined(&scenario.lists, true);
    let trusted_rules = joined(&scenario.trusted_lists, false);
    let engine = libweb_content_blocker_rust::rust_content_blocker_create(
        ak::Span::new(&rules),
        ak::Span::new(&trusted_rules),
        ak::Span::new(&[]),
    );
    let engine = unsafe { engine.as_ref() };
    assert!(engine.is_some());

    let rules = String::from_utf8(rules).unwrap();
    let trusted_rules = String::from_utf8(trusted_rules).unwrap();
    let mut filter_set = adblock::lists::FilterSet::new(false);
    filter_set.add_filter_list(rules.clone(), adblock::lists::ParseOptions::default());
    filter_set.add_filter_list(
        trusted_rules.clone(),
        adblock::lists::ParseOptions {
            permissions: adblock::resources::PermissionMask::from_bits(0x01),
            ..Default::default()
        },
    );
    let reference = adblock::engine::Engine::new_with_filter_set(filter_set);

    let mut urls = Vec::new();
    for line in rules.lines().chain(trusted_rules.lines()) {
        let Some(pattern) = line
            .strip_prefix("||")
            .and_then(|rest| rest.strip_suffix("^$document").or_else(|| rest.strip_suffix("^$doc")))
        else {
            continue;
        };
        if pattern.contains(['*', '^', '|', '$']) {
            continue;
        }
        // Ladybird serializes URLs with lowercase hosts, and the engine doesn't lowercase them itself, so only the path
        // gets another case.
        let (host, path) = pattern.split_at(pattern.find('/').unwrap_or(pattern.len()));
        urls.push(("exact", format!("https://{pattern}")));
        urls.push(("query", format!("https://{pattern}?x=1")));
        urls.push(("subdomain", format!("http://www.{pattern}")));
        urls.push(("longer", format!("https://{pattern}x")));
        urls.push(("path case", format!("https://{host}{}", path.to_ascii_uppercase())));
        urls.push(("port", format!("https://{host}:8443{path}")));
    }
    urls.extend(
        [
            "https://github.com/LadybirdBrowser/ladybird",
            "https://www.youtube.com/watch?v=x",
            "https://x.com/home",
            "https://example.com/",
        ]
        .map(|url| ("other", url.to_string())),
    );

    let source = "https://example.org/";
    let mut checked = 0;
    let mut blocked = 0;
    let mut mismatches = Vec::new();
    let mut mismatches_by_kind: std::collections::BTreeMap<String, usize> = Default::default();
    for (kind, url) in &urls {
        for request_type in ["document", "subdocument", "image"] {
            let Ok(request) = adblock::request::Request::new(url, source, request_type, "GET") else {
                continue;
            };
            let expected = reference.check_network_request(&request).should_block();
            let actual = libweb_content_blocker_rust::rust_content_blocker_matches(
                engine,
                ak::Span::new(url.as_bytes()),
                ak::Span::new(source.as_bytes()),
                ak::Span::new(request_type.as_bytes()),
            );
            checked += 1;
            blocked += usize::from(actual);
            if expected != actual {
                *mismatches_by_kind.entry(format!("{kind} {request_type} expected {expected}")).or_default() += 1;
                mismatches.push(format!("{kind} {request_type} {url}: expected {expected}, got {actual}"));
            }
        }
    }
    println!(
        "Verified {checked} requests to {} URLs ({blocked} blocked): {} mismatches",
        urls.len(),
        mismatches.len()
    );
    for (kind, count) in &mismatches_by_kind {
        println!("    {count} x {kind}");
    }
    for mismatch in mismatches.iter().take(20) {
        println!("    {mismatch}");
    }
}

// What the lists put in a page at each URL: hiding selectors, procedural filters, and scriptlets.
fn cosmetics(scenario: &Scenario) {
    let rules = String::from_utf8(joined(&scenario.lists, true)).unwrap();
    let trusted_rules = String::from_utf8(joined(&scenario.trusted_lists, false)).unwrap();
    let mut filter_set = adblock::lists::FilterSet::new(false);
    filter_set.add_filter_list(rules, adblock::lists::ParseOptions::default());
    filter_set.add_filter_list(
        trusted_rules,
        adblock::lists::ParseOptions {
            permissions: adblock::resources::PermissionMask::from_bits(0x01),
            ..Default::default()
        },
    );
    let mut engine = adblock::engine::Engine::new_with_filter_set(filter_set);
    if let Some(path) = &scenario.resources {
        let resources: Vec<adblock::resources::Resource> =
            serde_json::from_slice(&std::fs::read(path).unwrap()).unwrap();
        engine.use_resources(resources);
    }
    for url in [
        "https://www.youtube.com/",
        "https://www.youtube.com/watch?v=dQw4w9WgXcQ",
        "https://github.com/LadybirdBrowser/ladybird",
        "https://x.com/",
        "https://example.com/",
    ] {
        let resources = engine.url_cosmetic_resources(url);
        let selector_bytes: usize = resources.hide_selectors.iter().map(String::len).sum();
        let procedural_bytes: usize = resources.procedural_actions.iter().map(String::len).sum();
        println!(
            "{url}: {} hiding selectors ({selector_bytes} bytes), {} procedural filters ({procedural_bytes} bytes), \
             scriptlets {} bytes, generichide {}",
            resources.hide_selectors.len(),
            resources.procedural_actions.len(),
            resources.injected_script.len(),
            resources.generichide
        );
        let mut procedural: Vec<_> = resources.procedural_actions.iter().collect();
        procedural.sort();
        for action in procedural.iter().take(8) {
            println!("    procedural: {}", action.chars().take(160).collect::<String>());
        }
    }
}

fn main() {
    let arguments: Vec<String> = std::env::args().collect();
    let directory = PathBuf::from(&arguments[1]);
    let scenarios = scenarios(&directory);

    if arguments.len() > 2 {
        match arguments[2].as_str() {
            "webcontent" => measure_webcontent(&scenarios[arguments[3].parse::<usize>().unwrap()]),
            "adblock" => measure_adblock(
                &scenarios[arguments[3].parse::<usize>().unwrap()],
                arguments.get(4).map(Path::new),
            ),
            "deserialize" => measure_deserialize(Path::new(&arguments[3])),
            "verify" => verify(&scenarios[0]),
            "cosmetics" => cosmetics(&scenarios[0]),
            mode => panic!("unknown mode {mode}"),
        }
        return;
    }

    let executable = std::env::current_exe().unwrap();
    let run = |arguments: &[&str]| {
        let output = Command::new(&executable).arg(&directory).args(arguments).output().unwrap();
        if !output.status.success() {
            eprintln!("{arguments:?} failed: {}", String::from_utf8_lossy(&output.stderr));
        }
        print!("{}", String::from_utf8_lossy(&output.stdout));
    };

    let baseline = memory();
    println!("Process baseline: {} MiB (current), {} MiB (peak)", mib(baseline.current), mib(baseline.peak));
    println!();
    println!("scenario\tmode\ttext (MiB)\tbuild (ms)\tpeak while building (MiB)\tkept after building (MiB)\tserialized (MiB)");

    let serialized = std::env::temp_dir().join("xenon-content-blocker.dat");
    for (index, _) in scenarios.iter().enumerate() {
        run(&["webcontent", &index.to_string()]);
        if index < 3 {
            if index == 0 {
                run(&["adblock", &index.to_string(), serialized.to_str().unwrap()]);
            } else {
                run(&["adblock", &index.to_string()]);
            }
        }
    }
    run(&["deserialize", serialized.to_str().unwrap()]);
    println!();
    run(&["cosmetics"]);
    println!();
    run(&["verify"]);
}
