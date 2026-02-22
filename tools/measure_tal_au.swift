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
    "modulation": 0,
    "volume": 1,
    "masterfinetune": 2,
    "octavetranspose": 3,
    "portamentomode": 4,
    "portamentointensity": 5,
    "midilearn": 6,
    "midilearndelete": 7,
    "polymode": 8,
    "panic": 9,
    "lforate": 10,
    "lfowaveform": 11,
    "lfotrigger": 12,
    "lfosync": 13,
    "lfoinverted": 14,
    "dcolfovalue": 15,
    "dcopwmvalue": 16,
    "dcopwmmode": 17,
    "dcorange": 18,
    "dcolfovaluesnap": 19,
    "pulsevolume": 20,
    "sawvolume": 21,
    "suboscvolume": 22,
    "suboscmode": 23,
    "noisevolume": 24,
    "whitenoiseenabled": 25,
    "filtercutoff": 26,
    "filterresonance": 27,
    "filterenvelopevalue": 28,
    "filtermodulationvalue": 29,
    "filterkeyboardvalue": 30,
    "filterenvelopevaluefullrange": 31,
    "filtervolumecorrection": 32,
    "vcamode": 33,
    "adsrmode": 34,
    "adsrattack": 35,
    "adsrdecay": 36,
    "adsrsustain": 37,
    "adsrrelease": 38,
    "controlbenderfilter": 39,
    "controlbenderdco": 40,
    "controlbendermodulation": 41,
    "controlvelocityvolume": 42,
    "controlvelocityenvelope": 43,
    "arpseqhold": 45,
    "arpseqsyncenabled": 46,
    "arpseqtempo": 47,
    "arpseqlength": 48,
    "arpenabled": 49,
    "arpmode": 50,
    "seqenabled": 52,
    "seqrecord": 53,
    "portamentolinear": 54,
    "resetseq": 55,
    "mainsyncmode": 56,
    "adsrdecklick": 57,
    "loadsequencerpreset": 58,
    "arpseqshuffle": 59,
    "noisefloor": 60,
    "fmpulse": 61,
    "fmsaw": 62,
    "fmsubosc": 63,
    "fmnoise": 64,
    "fmintensity": 65,
    "seqpauserecord": 66,
    "engineoff": 67,
    "presetup": 68,
    "presetdown": 69,
    "midilock": 70,
    "mpeEnabled": 71,
]

guard CommandLine.arguments.count >= 2 else {
    fputs("usage: measure_tal_au.swift <preset.vstpreset>\n", stderr)
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
var line = ""

AVAudioUnit.instantiate(with: desc, options: []) { avUnit, err in
    defer { sem.signal() }
    if let err = err {
        caughtErr = err
        return
    }
    guard let unit = avUnit as? AVAudioUnitMIDIInstrument else {
        line = "{\"error\":\"not_midi_instrument\"}"
        return
    }

    let engine = AVAudioEngine()
    engine.attach(unit)
    engine.connect(unit, to: engine.mainMixerNode, format: nil)
    let format = engine.mainMixerNode.outputFormat(forBus: 0)

    do {
        try engine.enableManualRenderingMode(.offline, format: format, maximumFrameCount: 512)
        try engine.start()
    } catch {
        caughtErr = error
        return
    }

    if let tree = unit.auAudioUnit.parameterTree {
        for (k, v) in attrs {
            guard let addr = map[k], let p = tree.parameter(withAddress: addr) else { continue }
            p.value = v
        }
    }

    unit.startNote(60, withVelocity: 110, onChannel: 0)

    let frames = 128
    let totalBlocks = 64
    guard let buffer = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat,
                                        frameCapacity: AVAudioFrameCount(frames)) else {
        line = "{\"error\":\"buffer_alloc\"}"
        return
    }

    var samples: [Float] = []
    samples.reserveCapacity(totalBlocks * frames)
    var peak: Float = 0

    for _ in 0..<totalBlocks {
        do {
            let status = try engine.renderOffline(AVAudioFrameCount(frames), to: buffer)
            if status != .success && status != .insufficientDataFromInputNode { break }
            let n = Int(buffer.frameLength)
            if let ch0 = buffer.floatChannelData?[0] {
                for i in 0..<n {
                    let s = ch0[i]
                    if abs(s) > peak { peak = abs(s) }
                    samples.append(s)
                }
            }
        } catch {
            caughtErr = error
            return
        }
    }

    let start = max(0, samples.count - 16 * frames)
    if samples.count - start < 4 {
        line = "{\"error\":\"too_short\"}"
        return
    }

    var zc = 0
    var absMean: Float = 0
    for i in (start + 1)..<samples.count {
        let a = samples[i - 1]
        let b = samples[i]
        if (a >= 0 && b < 0) || (a < 0 && b >= 0) { zc += 1 }
        absMean += abs(b)
    }
    absMean /= Float(samples.count - start - 1)

    var best: Float = 0
    var bestLag = 0
    let lagMin = 40
    let lagMax = min(500, samples.count - start - 2)
    if lagMax > lagMin {
        for lag in lagMin...lagMax {
            var acc: Double = 0
            var e1: Double = 0
            var e2: Double = 0
            var i = start + lag
            while i < samples.count {
                let x = Double(samples[i])
                let y = Double(samples[i - lag])
                acc += x * y
                e1 += x * x
                e2 += y * y
                i += 1
            }
            if e1 > 1e-9 && e2 > 1e-9 {
                let r = Float(acc / sqrt(e1 * e2))
                if r > best {
                    best = r
                    bestLag = lag
                }
            }
        }
    }

    line = String(
        format: "{\"name\":\"%@\",\"peak\":%.6f,\"absmean\":%.6f,\"zc_rate\":%.6f,\"autocorr\":%.6f,\"lag\":%d}",
        presetName, peak, absMean, Float(zc) / Float(max(1, samples.count - start - 1)), best, bestLag
    )
}

_ = sem.wait(timeout: .now() + 20)
if let e = caughtErr {
    fputs("error: \(e)\n", stderr)
    exit(1)
}
print(line)
