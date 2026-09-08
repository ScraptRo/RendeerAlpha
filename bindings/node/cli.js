#!/usr/bin/env node
// The toolchain, from a Node project.
//
//     npx rda build [project] [--force]    compile the interface under <project>/res/
//     npx rda where                        which engine and tool this would use
//
// `build` runs the engine's `rda build <project> --node`: every theme to its .rdth, every
// layout to its .rdab, res/layouts/rda.d.ts, and state.mjs into the project directory --
// skipping whatever is already newer than its source. Run it once before the first
// `node app.mjs`, and again whenever res/state.ts changes; a running Debug engine
// recompiles an edited layout by itself.
//
// Anything else is handed to the tool as it is, so `npx rda dump res/layouts/home.rdab`
// works too. Self-contained on purpose: this must run before `npm install` has put
// koffi anywhere, so it imports nothing from the rest of the package.

import { spawnSync } from 'node:child_process'
import { existsSync, readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import process from 'node:process'

const exe = process.platform === 'win32' ? 'rda.exe' : 'rda'

// Where the tool is, nearest first: RDA_ENGINE, then the checkout's bin/ this package
// lives in, then the working directory, then PATH.
function* candidates() {
	const fromEnv = process.env.RDA_ENGINE
	if (fromEnv) yield fromEnv.endsWith(exe) ? fromEnv : join(fromEnv, exe)
	yield join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'bin', exe)
	yield join(process.cwd(), exe)
}

function find() {
	for (const candidate of candidates()) {
		if (existsSync(candidate)) return candidate
	}
	return exe   // and let PATH decide
}

const args = process.argv.slice(2)
if (args.length === 0 || args[0] === '-h' || args[0] === '--help' || args[0] === 'help') {
	console.log('rda build [project] [--force]   compile the interface under <project>/res/')
	console.log('rda where                       which engine and tool this would use')
	console.log('rda <anything else>             handed to the toolchain as it is')
	process.exit(args.length === 0 ? 1 : 0)
}

const tool = find()
if (args[0] === 'where') {
	const home = dirname(tool)
	console.log(`engine:  ${existsSync(tool) ? home : 'not found'}`)
	console.log(`tool:    ${existsSync(tool) ? tool : 'not found'}`)
	const stamp = join(home, 'BUILD.txt')
	if (existsSync(stamp)) console.log(`built:   ${readFileSync(stamp, 'utf8').trim()}`)
	process.exit(existsSync(tool) ? 0 : 1)
}

const command = [...args]
if (args[0] === 'build' && !args.some(a => ['--python', '--node', '--csharp', '--cpp'].includes(a))) {
	command.push('--node')   // a Node project, so the state comes out as an ES module
}

const result = spawnSync(tool, command, { stdio: 'inherit' })
if (result.error) {
	console.error(`rda: cannot run ${tool}: ${result.error.message}`)
	console.error('     Build the engine and stage it -- scripts/windows-bringup.bat or ' +
	              'scripts/linux-bringup.sh in the checkout -- or set RDA_ENGINE to the ' +
	              'folder holding it.')
	process.exit(1)
}
process.exit(result.status ?? 1)
