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
    guard let str = String(data: data, encoding: .utf8) ?? String(data: data, encoding: .isoLatin1) else {
        return [:]
    }
    guard let talRange = str.range(of: "<tal "),
          let talEndRange = str.range(of: "</tal>") else {
        return [:]
    }
    let xml = String(str[talRange.lowerBound..<talEndRange.upperBound])
    guard let progStart = xml.range(of: "<program "),
          let progEnd = xml.range(of: "/>", range: progStart.lowerBound..<xml.endIndex) else {
        return [:]
    }
    let prog = String(xml[progStart.lowerBound..<progEnd.upperBound])
    let pattern = " ([A-Za-z0-9_]+)=\\\"([^\\\"]*)\\\""
    let regex = try NSRegularExpression(pattern: pattern)
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

guard CommandLine.arguments.count >= 2 else {
    fputs("usage: diagnose_tal_au <preset.vstpreset>\n", stderr)
    exit(2)
}

let presetPath = CommandLine.arguments[1]
let attrs = try readPresetAttrs(presetPath)
let presetName = URL(fileURLWithPath: presetPath).deletingPathExtension().lastPathComponent

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

    // Read noisefloor parameter before and after setting preset
    var noisefloorBefore: Float = -1
    var noisefloorAfter: Float = -1
    if let tree = unit.auAudioUnit.parameterTree,
       let nfParam = tree.parameter(withAddress: 60) {
        noisefloorBefore = nfParam.value
    }

    if let tree = unit.auAudioUnit.parameterTree {
        for (k, v) in attrs {
            guard let addr = map[k], let p = tree.parameter(withAddress: addr) else { continue }
            p.value = v
        }
    }

    if let tree = unit.auAudioUnit.parameterTree,
       let nfParam = tree.parameter(withAddress: 60) {
        noisefloorAfter = nfParam.value
    }

    unit.startNote(60, withVelocity: 110, onChannel: 0)

    let frames = 128
    let totalBlocks = 64
    guard let buffer = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat,
                                        frameCapacity: AVAudioFrameCount(frames)) else { return }

    var allSamples: [Float] = []
    allSamples.reserveCapacity(totalBlocks * frames)
    var blockRMS: [Float] = []

    for _ in 0..<totalBlocks {
        do {
            let status = try engine.renderOffline(AVAudioFrameCount(frames), to: buffer)
            if status != .success && status != .insufficientDataFromInputNode { break }
            let n = Int(buffer.frameLength)
            var sumSq: Float = 0
            if let ch0 = buffer.floatChannelData?[0] {
                for i in 0..<n {
                    let s = ch0[i]
                    allSamples.append(s)
                    sumSq += s * s
                }
            }
            blockRMS.append(sqrt(sumSq / Float(max(1, n))))
        } catch { caughtErr = error; return }
    }

    // Per-block analysis for measurement window (blocks 48-64)
    let start = 48 * frames
    let end = 64 * frames
    var measPeak: Float = 0
    var measAbsmean: Float = 0
    var measZC = 0
    for i in start..<min(end, allSamples.count) {
        let s = allSamples[i]
        if abs(s) > measPeak { measPeak = abs(s) }
        measAbsmean += abs(s)
        if i > start {
            let prev = allSamples[i-1]
            if (prev >= 0 && s < 0) || (prev < 0 && s >= 0) { measZC += 1 }
        }
    }
    let measN = min(end, allSamples.count) - start
    measAbsmean /= Float(max(1, measN))

    // Autocorrelation in measurement window
    var bestAC: Float = 0
    var bestLag = 0
    for lag in 40...min(500, measN - 2) {
        var acc: Double = 0, e1: Double = 0, e2: Double = 0
        for i in (start + lag)..<min(end, allSamples.count) {
            let x = Double(allSamples[i])
            let y = Double(allSamples[i - lag])
            acc += x * y
            e1 += x * x
            e2 += y * y
        }
        if e1 > 1e-9 && e2 > 1e-9 {
            let r = Float(acc / sqrt(e1 * e2))
            if r > bestAC { bestAC = r; bestLag = lag }
        }
    }

    // Print diagnostics
    print("preset: \(presetName)")
    print("noisefloor: before=\(noisefloorBefore) after=\(noisefloorAfter)")
    print("TAL attrs: noisefloor=\(attrs["noisefloor"] ?? -1)")
    print("measurement: peak=\(String(format:"%.6f", measPeak)) absmean=\(String(format:"%.6f", measAbsmean)) zc_rate=\(String(format:"%.6f", Float(measZC)/Float(max(1,measN-1)))) autocorr=\(String(format:"%.6f", bestAC)) lag=\(bestLag)")
    print("per_block_rms:")
    for (i, rms) in blockRMS.enumerated() {
        let marker = (i >= 48) ? " *MEAS*" : ""
        print("  block \(String(format:"%2d", i)): rms=\(String(format:"%.6f", rms))\(marker)")
    }
}

_ = sem.wait(timeout: .now() + 30)
if let e = caughtErr {
    fputs("error: \(e)\n", stderr)
    exit(1)
}
