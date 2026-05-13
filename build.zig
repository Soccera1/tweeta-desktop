const std = @import("std");

const system_libs = [_][]const u8{
    "gtk+-3.0",
    "json-glib-1.0",
    "libcurl",
    "gpgme",
};

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{
        .default_target = .{
            .cpu_arch = .x86_64,
            .os_tag = .linux,
            .abi = .gnu,
        },
    });
    const optimize = b.standardOptimizeOption(.{});
    const use_fido2 = b.option(bool, "fido2", "Enable native FIDO2/passkey support") orelse false;
    const static_link = b.option(bool, "static", "Attempt to statically link the executable") orelse false;
    const globals_obj = buildZigObject(b, "globals", "src/globals.zig", target, optimize, use_fido2);
    const network_obj = buildZigObject(b, "network", "src/network.zig", target, optimize, use_fido2);
    const session_obj = buildZigObject(b, "session", "src/session.zig", target, optimize, use_fido2);
    const challenge_obj = buildZigObject(b, "challenge", "src/challenge.zig", target, optimize, use_fido2);
    const ui_utils_obj = buildZigObject(b, "ui_utils", "src/ui_utils.zig", target, optimize, use_fido2);
    const p2p_crypto_obj = buildZigObject(b, "p2p_crypto", "src/p2p_crypto.zig", target, optimize, use_fido2);
    const p2p_network_obj = buildZigObject(b, "p2p_network", "src/p2p_network.zig", target, optimize, use_fido2);
    const actions_p2p_network_obj = buildZigObject(b, "actions_p2p_network", "src/actions_p2p_network.zig", target, optimize, use_fido2);
    const webauthn_fido2_obj = buildZigObject(b, "webauthn_fido2", "src/webauthn_fido2.zig", target, optimize, use_fido2);
    const json_utils_obj = buildZigObject(b, "json_utils", "src/json_utils.zig", target, optimize, use_fido2);
    const views_obj = buildZigObject(b, "views", "src/views.zig", target, optimize, use_fido2);
    const ui_components_obj = buildZigObject(b, "ui_components", "src/ui_components.zig", target, optimize, use_fido2);
    const actions_obj = buildZigObject(b, "actions", "src/actions.zig", target, optimize, use_fido2);

    const exe_mod = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    configureCModule(b, exe_mod, use_fido2);
    exe_mod.addObject(globals_obj);
    exe_mod.addObject(network_obj);
    exe_mod.addObject(session_obj);
    exe_mod.addObject(challenge_obj);
    exe_mod.addObject(ui_utils_obj);
    exe_mod.addObject(p2p_crypto_obj);
    exe_mod.addObject(p2p_network_obj);
    exe_mod.addObject(actions_p2p_network_obj);
    exe_mod.addObject(webauthn_fido2_obj);
    exe_mod.addObject(json_utils_obj);
    exe_mod.addObject(views_obj);
    exe_mod.addObject(ui_components_obj);
    exe_mod.addObject(actions_obj);

    const exe = b.addExecutable(.{
        .name = if (static_link) "tweeta-desktop-static" else "tweeta-desktop",
        .root_module = exe_mod,
        .linkage = if (static_link) .static else null,
    });
    exe.linker_allow_shlib_undefined = true;
    b.installArtifact(exe);
    installDataFiles(b);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
    const run_step = b.step("run", "Run Tweeta Desktop");
    run_step.dependOn(&run_cmd.step);

    const test_mod = b.createModule(.{
        .root_source_file = b.path("src/test_main.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    configureCModule(b, test_mod, use_fido2);
    test_mod.addObject(globals_obj);
    test_mod.addObject(network_obj);
    test_mod.addObject(session_obj);
    test_mod.addObject(challenge_obj);
    test_mod.addObject(ui_utils_obj);
    test_mod.addObject(p2p_crypto_obj);
    test_mod.addObject(p2p_network_obj);
    test_mod.addObject(actions_p2p_network_obj);
    test_mod.addObject(webauthn_fido2_obj);
    test_mod.addObject(json_utils_obj);
    test_mod.addObject(views_obj);
    test_mod.addObject(ui_components_obj);
    test_mod.addObject(actions_obj);

    const test_runner = b.addExecutable(.{
        .name = "test_runner",
        .root_module = test_mod,
    });
    test_runner.linker_allow_shlib_undefined = true;

    const run_tests = b.addRunArtifact(test_runner);
    const test_step = b.step("test", "Build and run the GLib test suite");
    test_step.dependOn(&run_tests.step);
}

fn buildZigObject(
    b: *std.Build,
    name: []const u8,
    path: []const u8,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    use_fido2: bool,
) *std.Build.Step.Compile {
    const options = b.addOptions();
    options.addOption(bool, "use_fido2", use_fido2);

    const mod = b.createModule(.{
        .root_source_file = b.path(path),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    mod.addImport("config", options.createModule());
    configureSystemPaths(mod);
    linkSystemLibraries(mod, use_fido2);
    return b.addObject(.{
        .name = name,
        .root_module = mod,
    });
}

fn configureCModule(b: *std.Build, mod: *std.Build.Module, use_fido2: bool) void {
    _ = b;
    configureSystemPaths(mod);
    linkSystemLibraries(mod, use_fido2);
}

fn configureSystemPaths(mod: *std.Build.Module) void {
    addSystemIncludePathIfExists(mod, "/usr/local/include");
    addSystemIncludePathIfExists(mod, "/usr/include");
    addSystemIncludePathIfExists(mod, "/usr/include/x86_64-linux-gnu");
    addLibraryPathIfExists(mod, "/usr/lib64");
    addLibraryPathIfExists(mod, "/usr/lib");
    addLibraryPathIfExists(mod, "/usr/lib/x86_64-linux-gnu");
}

fn linkSystemLibraries(mod: *std.Build.Module, use_fido2: bool) void {
    for (system_libs) |lib| {
        mod.linkSystemLibrary(lib, .{ .use_pkg_config = .force });
    }

    if (use_fido2) {
        mod.linkSystemLibrary("libfido2", .{ .use_pkg_config = .force });
    }
}

fn addSystemIncludePathIfExists(mod: *std.Build.Module, path: []const u8) void {
    std.fs.accessAbsolute(path, .{}) catch return;
    mod.addSystemIncludePath(.{ .cwd_relative = path });
}

fn addLibraryPathIfExists(mod: *std.Build.Module, path: []const u8) void {
    std.fs.accessAbsolute(path, .{}) catch return;
    mod.addLibraryPath(.{ .cwd_relative = path });
}

fn installDataFiles(b: *std.Build) void {
    const install_step = b.getInstallStep();

    install_step.dependOn(&b.addInstallFileWithDir(
        b.path("tweeta-desktop.desktop"),
        .{ .custom = "share/applications" },
        "tweeta-desktop.desktop",
    ).step);
    install_step.dependOn(&b.addInstallFileWithDir(
        b.path("logo.png"),
        .{ .custom = "share/pixmaps" },
        "tweeta-desktop.png",
    ).step);
    install_step.dependOn(&b.addInstallFileWithDir(
        b.path("tweeta-desktop.1"),
        .{ .custom = "share/man/man1" },
        "tweeta-desktop.1",
    ).step);

    const makeinfo = b.findProgram(&.{"makeinfo"}, &.{}) catch null;
    if (makeinfo) |makeinfo_path| {
        const makeinfo_cmd = b.addSystemCommand(&.{makeinfo_path});
        makeinfo_cmd.addArg("-o");
        const info_file = makeinfo_cmd.addOutputFileArg("tweeta-desktop.info");
        makeinfo_cmd.addFileArg(b.path("tweeta-desktop.texi"));
        install_step.dependOn(&b.addInstallFileWithDir(
            info_file,
            .{ .custom = "share/info" },
            "tweeta-desktop.info",
        ).step);
    }
}
