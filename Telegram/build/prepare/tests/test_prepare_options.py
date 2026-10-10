"""Check prepare commands without importing the script or building dependencies."""
import ast
import contextlib
import io
import hashlib
import os
from pathlib import Path
import re
import tempfile
import types
import unittest


SOURCE = Path(__file__).resolve().parents[1] / 'prepare.py'
TREE = ast.parse(SOURCE.read_text(encoding='utf-8'))


def functions(platform='win64', options=()):
    namespace = dict(
        re=re, os=os, keysLoc='cache_keys',
        win=platform != 'mac', win32=platform == 'win32',
        win64=platform == 'win64', winarm=platform == 'winarm',
        mac=platform == 'mac', options=options, qt='6.11.1',
        branch='v$QT', rustToolchain='1.90.0', modifiedEnv={},
        scriptPath=str(SOURCE.parent),
        computeFileHash=lambda path: hashlib.sha1(Path(path).read_bytes()).hexdigest(),
        subprocess=types.SimpleNamespace(run=lambda *a, **k:
            types.SimpleNamespace(stdout='Python 3.12.10')),
    )
    # Evaluate the shared recipe fragment without importing prepare.py, which
    # would execute dependency builds and change the working directory.
    for node in TREE.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id in {
                        'macBreakpadBuild', 'rustToolchain',
                        'tlottieRevision', 'walletEngineRevision'}:
                    namespace[target.id] = ast.literal_eval(node.value)
    names = {'filterByPlatform', 'removeDir', 'setVar', 'dependencyGroup',
             'keyPath', 'checkCacheKey', 'runStages'}
    for node in TREE.body:
        if isinstance(node, ast.FunctionDef) and node.name in names:
            exec(compile(ast.Module(body=[node], type_ignores=[]),
                         str(SOURCE), 'exec'), namespace)
    return namespace


def stage_commands(platform, options=()):
    namespace = functions(platform, options)
    for node in ast.walk(TREE):
        if isinstance(node, ast.Call) and getattr(node.func, 'id', '') == 'stage':
            args = [eval(compile(ast.Expression(arg), str(SOURCE), 'eval'), namespace)
                    for arg in node.args]
            commands, _, _ = namespace['filterByPlatform'](args[1])
            yield args[0], commands


class PrepareOptionsTests(unittest.TestCase):
    def test_qt5_patch_invalidates_the_preparation_cache(self):
        recipe = next(node for node in ast.walk(TREE)
            if isinstance(node, ast.Call) and getattr(node.func, 'id', '') == 'stage'
            and 'qt5-static-angle.patch' in ast.unparse(node))
        namespace = functions(options=('skip-debug',))
        commands = eval(compile(ast.Expression(recipe.args[1]), str(SOURCE), 'eval'),
            namespace)
        filtered, _, version = namespace['filterByPlatform'](commands)
        expected = hashlib.sha1(
            (SOURCE.parent / 'qt5-static-angle.patch').read_bytes()).hexdigest()
        self.assertIn(expected, version)
        self.assertIn('git apply "', filtered)
        self.assertIn('qt5-static-angle.patch"\nif errorlevel 1 exit /b 1', filtered)

    def test_plugin_engine_dependency_is_prepared(self):
        telegram = SOURCE.parents[2]
        self.assertIn('#include <QJSEngine>',
            (telegram / 'SourceFiles/jel/plugins/plugin_worker.cpp').read_text())
        self.assertIn('REQUIRED COMPONENTS Qml',
            (telegram / 'CMakeLists.txt').read_text())
        for platform in ('win32', 'win64', 'winarm', 'mac'):
            qt_recipes = [commands for name, commands in stage_commands(platform)
                if name.startswith('qt_')]
            self.assertTrue(qt_recipes)
            for commands in qt_recipes:
                with self.subTest(platform=platform):
                    self.assertRegex(commands,
                        r'git submodule update[^\n]*\bqtdeclarative\b')
        dockerfile = (telegram / 'build/docker/centos_env/Dockerfile').read_text()
        self.assertRegex(dockerfile,
            r'git submodule update[^\n]*\\\n[^\n]*\bqtdeclarative\b')

    def test_release_only_has_no_debug_builds(self):
        debug_command = re.compile(
            r'^.*(?:--(?:build|install).*--config Debug|Configuration=Debug|'
            r'builddir-debug|meson .*out/Debug|ninja -C out/Debug|'
            r'CFG=debug-static|debug-VC-WIN)', re.MULTILINE)
        for platform in ('win32', 'win64', 'winarm'):
            for name, commands in stage_commands(platform, ('skip-debug',)):
                with self.subTest(platform=platform, stage=name):
                    self.assertNotRegex(commands, debug_command)
                    if name == 'openssl3':
                        self.assertNotIn('out.dbg', commands)
                        self.assertNotIn('jom clean', commands)
                        self.assertIn('mkdir out\n', commands)
                    if name == 'tde2e':
                        self.assertIn('cd out\nmkdir Release\ncd Release\n', commands)
                        self.assertNotIn('mkdir Debug', commands)
                    if name.startswith('qt_'):
                        self.assertIn('SET CONFIGURATIONS=-release\n', commands)

    def test_debug_guard_does_not_enable_wrong_platform(self):
        commands = 'win:\ndebug:\n debug-build\nenddebug:\n release-build\nmac:\n mac-build\n'
        filtered = functions('mac')['filterByPlatform'](commands)[0]
        self.assertEqual(filtered, 'mac-build\n')
        default = functions()['filterByPlatform'](commands)[0]
        self.assertEqual(default, 'debug-build\nrelease-build\n')
        release = functions(options=('skip-debug',))['filterByPlatform'](commands)[0]
        self.assertEqual(release, 'release-build\n')

    def test_grouped_prepare_reuses_completed_stages(self):
        for option, selected in (('tools-only', 'python'),
                                 ('libraries-only', 'library'),
                                 ('qt-only', 'qt_6.11.1')):
            with self.subTest(group=option), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / 'cache_keys').mkdir()
                (root / selected).mkdir()
                (root / 'cache_keys' / selected).write_text('current', encoding='utf-8')
                namespace = functions(options=('skip-debug', option))
                evaluated = []
                def compute(stage):
                    evaluated.append(stage['name'])
                    return 'current'
                namespace.update(
                    sys=types.SimpleNamespace(argv=['prepare.py', 'silent', 'skip-debug', option]),
                    stages=[dict(name=name, location=location, directory=directory,
                                 version='0', commands='must-not-build')
                            for name, location in [('python', 'ThirdParty'),
                                                   ('library', 'Libraries'),
                                                   ('qt_6.11.1', 'Libraries')]],
                    computeCacheKey=compute,
                    run=lambda commands: self.fail('A completed dependency was rebuilt'),
                )
                with contextlib.redirect_stdout(io.StringIO()):
                    namespace['runStages']()
                self.assertEqual(evaluated, [selected])

    def test_partial_cache_requires_completed_stage_marker(self):
        check = functions()['checkCacheKey']
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'cache_keys').mkdir()
            stage = dict(name='library', directory=directory, key='current')
            self.assertEqual(check(stage), 'NotFound')
            (root / 'library').mkdir()
            self.assertEqual(check(stage), 'Stale')
            marker = root / 'cache_keys' / 'library'
            marker.write_text('previous', encoding='utf-8')
            self.assertEqual(check(stage), 'Stale')
            marker.write_text('current', encoding='utf-8')
            self.assertEqual(check(stage), 'Good')


if __name__ == '__main__':
    unittest.main()
