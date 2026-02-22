// Renders a .vstpreset through TAL BassLine 101 AU and writes a WAV file.
// Usage: swift render_tal_au_wav.swift <preset.vstpreset> <output.wav>
import Foundation
import AVFoundation
import AudioToolbox

func fourCC(_ s: String) -> FourCharCode {
    var result: FourCharCode = 0
    for u in s.utf8 { result = (result << 8) + FourCharCode(u) }
    return result
}

func readPresetAttrs(_ path: String) throws -> [String: Float] {
    let data = try Data(contentsOf: URL(fileURLWithPath: path))
    guard let str = String(data: data, encoding: .utf8) ?? String(data: data, encoding: .isoLatin1) else { return [:] }
    guard let talRange = str.range(of: "<tal "),
          let talEndRange = str.range(of: "</tal>") else { return [:] }
    let xml = String(str[talRange.lowerBound..<talEndRange.upperBound])
    guard let progStart = xml.range(of: "<program "),
          let progEnd = xml.range(of: "/>", range: progStart.lowerBound..<xml.endIndex) else {
        // Try closing tag variant
        guard let ps2 = xml.range(of: "<program "),
              let pe2 = xml.range(of: ">", range: ps2.lowerBound..<xml.endIndex) else { return [:] }
        let prog = String(xml[ps2.lowerBound..<pe2.upperBound])
        return parseAttrs(prog)
    }
    let prog = String(xml[progStart.lowerBound..<progEnd.upperBound])
    return parseAttrs(prog)
}

func parseAttrs(_ prog: String) -> [String: Float] {
    let pattern = " ([A-Za-z0-9_]+)=\"([^\"]*)\""
    guard let regex = try? NSRegularExpression(pattern: pattern) else { return [:] }
    let ns = prog as NSString
    let matches = regex.matches(in: prog, range: NSRange(location: 0, length: ns.length))
    var out: [String: Float] = [:]
    for m in matches {
        let k = ns.substring(with: m.range(at: 1))
        let v = ns.substring(with: m.range(at: 2))
        if let f = Float(v) { out[k] = f }
    }
    return out
}

let map: [String: AUParameterAddress] = [
    "modulation": 0, "volume": 1, "masterfinetune": 2, "octavetranspose": 3,
    "portamentomode": 4, "portamentointensity": 5, "polymode": 8,
    "lforate": 10, "lfowaveform": 11, "lfotrigger": 12, "lfosync": 13,
    "lfoinverted": 14, "dcolfovalue": 15, "dcopwmvalue": 16, "dcopwmmode": 17,
    "dcorange": 18, "dcolfovaluesnap": 19, "pulsevolume": 20, "sawvolume": 21,
    "suboscvolume": 22, "suboscmode": 23, "noisevolume": 24, "whitenoiseenabled": 25,
    "filtercutoff": 26, "filterresonance": 27, "filterenvelopevalue": 28,
    "filtermodulationvalue": 29, "filterkeyboardvalue": 30,
    "filterenvelopevaluefullrange": 31, "filtervolumecorrection": 32,
    "vcamode": 33, "adsrmode": 34, "adsrattack": 35, "adsrdecay": 36,
    "adsrsustain": 37, "adsrrelease": 38, "controlbenderfilter": 39,
    "controlbenderdco": 40, "controlbendermodulation": 41,
    "controlvelocityvolume": 42, "controlvelocityenvelope": 43,
    "portamentolinear": 54, "adsrdecklick": 57, "noisefloor": 60,
    "fmpulse": 61, "fmsaw": 62, "fmsubosc": 63, "fmnoise": 64, "fmintensity": 65,
]

guard CommandLine.arguments.count >= 3 else {
    fputs("usage: render_tal_au_wav <preset.vstpreset> <output.wav>\n", stderr)
    exit(2)
}

let presetPath = CommandLine.arguments[1]
let wavPath = CommandLine.arguments[2]
let attrs = try readPresetAttrs(presetPath)

let desc = AudioComponentDescription(componentType: kAudioUnitType_MusicDevice,
                                     componentSubType: fourCC("bAs1"),
                                     componentManufacturer: fourCC("TOGU"),
                                     componentFlags: 0,
                                     componentFlagsMask: 0)
let sem = DispatchSemaphore(value: 0)
var caughtErr: Error?

AVAudioUnit.instantiate(with: desc, options: []) { avUnit, err in
    defer { sem.signal() }
    if let err = err { caughtErr = err; return }
    guard let unit = avUnit as? AVAudioUnitMIDIInstrument else { return }

    let engine = AVAudioEngine()
    engine.attach(unit)
    engine.connect(unit, to: engine.mainMixerNode, format: nil)
    let format = engine.mainMixerNode.outputFormat(forBus: 0)

    do {
        try engine.enableManualRenderingMode(.offline, format: format, maximumFrameCount: 512)
        try engine.start()
    } catch { caughtErr = error; return }

    if let tree = unit.auAudioUnit.parameterTree {
        for (k, v) in attrs {
            guard let addr = map[k], let p = tree.parameter(withAddress: addr) else { continue }
            p.value = v
        }
    }

    unit.startNote(60, withVelocity: 110, onChannel: 0)

    let frames = 128
    let totalBlocks = 96  // 64 note-on + 32 release
    guard let buffer = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat,
                                        frameCapacity: AVAudioFrameCount(frames)) else { return }

    var allSamples: [Float] = []
    allSamples.reserveCapacity(totalBlocks * frames)

    for b in 0..<totalBlocks {
        if b == 64 {
            unit.stopNote(60, onChannel: 0)
        }
        do {
            let status = try engine.renderOffline(AVAudioFrameCount(frames), to: buffer)
            if status != .success && status != .insufficientDataFromInputNode { break }
            let n = Int(buffer.frameLength)
            if let ch0 = buffer.floatChannelData?[0] {
                for i in 0..<n { allSamples.append(ch0[i]) }
            }
        } catch { caughtErr = error; return }
    }

    // Write WAV (16-bit mono)
    let totalSamples = allSamples.count
    var wavData = Data()
    // RIFF header
    wavData.append(contentsOf: "RIFF".utf8)
    var fileSize = UInt32(36 + totalSamples * 2)
    wavData.append(Data(bytes: &fileSize, count: 4))
    wavData.append(contentsOf: "WAVEfmt ".utf8)
    var fmtSize: UInt32 = 16; wavData.append(Data(bytes: &fmtSize, count: 4))
    var audioFmt: UInt16 = 1; wavData.append(Data(bytes: &audioFmt, count: 2))
    var channels: UInt16 = 1; wavData.append(Data(bytes: &channels, count: 2))
    var sampleRate: UInt32 = 44100; wavData.append(Data(bytes: &sampleRate, count: 4))
    var byteRate: UInt32 = 44100 * 2; wavData.append(Data(bytes: &byteRate, count: 4))
    var blockAlign: UInt16 = 2; wavData.append(Data(bytes: &blockAlign, count: 2))
    var bitsPerSample: UInt16 = 16; wavData.append(Data(bytes: &bitsPerSample, count: 2))
    wavData.append(contentsOf: "data".utf8)
    var dataSize = UInt32(totalSamples * 2)
    wavData.append(Data(bytes: &dataSize, count: 4))
    for s in allSamples {
        var sample = Int16(max(-32768, min(32767, Int32(s * 32767.0))))
        wavData.append(Data(bytes: &sample, count: 2))
    }

    do {
        try wavData.write(to: URL(fileURLWithPath: wavPath))
        fputs("wrote \(wavPath) (\(totalSamples) samples, \(String(format: "%.2f", Float(totalSamples)/44100.0))s)\n", stderr)
    } catch {
        fputs("error writing wav: \(error)\n", stderr)
    }
}

_ = sem.wait(timeout: .now() + 30)
if let e = caughtErr {
    fputs("error: \(e)\n", stderr)
    exit(1)
}
