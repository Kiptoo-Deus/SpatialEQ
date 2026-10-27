// Generates every icon and installer image from the box artwork:
//
//   swift branding/make_assets.swift
//
//   branding/source/emblem.png  Savannah DSP emblem (bronze tree + waveform) cropped from the box front
//   branding/source/slate.png   plain brushed-slate texture from the same artwork
//   branding/source/box.png     SpatialEQ product box render, transparent background
//
// Output (committed, so builds don't need to run this):
//   branding/icon/SpatialEQ.icns            macOS app, VST3 and AU bundles
//   branding/icon/SpatialEQ.ico             Windows app, VST3 folder icon, setup programs
//   branding/icon/SpatialEQ-1024.png        master icon (README, stores)
//   branding/installer/pkg-background.png   macOS Installer sidebar (+ @2x)
//   branding/installer/dmg-background.png   drag-to-Applications window (+ @2x)
//   branding/installer/wizard-*.bmp         Inno Setup wizard images (100-200 % DPI)
import AppKit
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
let source = root.appendingPathComponent("source")
let iconDir = root.appendingPathComponent("icon")
let installerDir = root.appendingPathComponent("installer")
for dir in [iconDir, installerDir] { try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true) }

func load(_ name: String) -> CGImage {
    let url = source.appendingPathComponent(name)
    guard let src = CGImageSourceCreateWithURL(url as CFURL, nil),
          let image = CGImageSourceCreateImageAtIndex(src, 0, nil) else { fatalError("cannot read \(url.path)") }
    return image
}

let sRGB = CGColorSpace(name: CGColorSpace.sRGB)!

/// The image's pixels as premultiplied RGBA, for the two pixel passes below.
func pixels(_ image: CGImage) -> (CGContext, UnsafeMutablePointer<UInt8>) {
    let ctx = CGContext(data: nil, width: image.width, height: image.height, bitsPerComponent: 8,
                        bytesPerRow: image.width * 4, space: sRGB, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
    return (ctx, ctx.data!.assumingMemoryBound(to: UInt8.self))
}

/// The emblem is cut from the slate artwork, so fade its edges out; drawn over the same slate, no seam shows.
func feathered(_ image: CGImage, sides: Double, top: Double, bottom: Double) -> CGImage {
    let (ctx, p) = pixels(image)
    let w = image.width, h = image.height
    func ramp(_ d: Double, _ width: Double) -> Double {
        guard width > 0 else { return 1 }
        let t = min(1, max(0, d / width))
        return t * t * (3 - 2 * t)
    }
    for y in 0..<h { // row 0 is the top of the image
        for x in 0..<w {
            let fx = Double(x) / Double(w - 1), fy = Double(y) / Double(h - 1)
            let a = ramp(fx, sides) * ramp(1 - fx, sides) * ramp(fy, top) * ramp(1 - fy, bottom)
            let i = (y * w + x) * 4
            for c in 0..<4 { p[i + c] = UInt8(Double(p[i + c]) * a) }
        }
    }
    return ctx.makeImage()!
}

/// Crops away fully transparent margins.
func trimmed(_ image: CGImage) -> CGImage {
    let (ctx, p) = pixels(image)
    let w = image.width, h = image.height
    var minX = w, minY = h, maxX = 0, maxY = 0
    withExtendedLifetime(ctx) { // owns the pixel buffer
        for y in 0..<h {
            for x in 0..<w where p[(y * w + x) * 4 + 3] > 8 {
                minX = min(minX, x); maxX = max(maxX, x); minY = min(minY, y); maxY = max(maxY, y)
            }
        }
    }
    return image.cropping(to: CGRect(x: minX, y: minY, width: maxX - minX + 1, height: maxY - minY + 1))!
}

let emblem = feathered(load("emblem.png"), sides: 0.1, top: 0.14, bottom: 0.04)
let slate = load("slate.png")
// The render's shadow runs off the left and bottom of the image; fade it instead of a hard edge.
let box = feathered(trimmed(load("box.png")), sides: 0.08, top: 0, bottom: 0.1)

func canvas(_ w: Int, _ h: Int, opaque: Bool = false) -> CGContext {
    let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0, space: sRGB,
                        bitmapInfo: (opaque ? CGImageAlphaInfo.noneSkipLast : CGImageAlphaInfo.premultipliedLast).rawValue)!
    ctx.interpolationQuality = .high
    return ctx
}

func write(_ image: CGImage, _ url: URL, _ type: UTType = .png) {
    let dest = CGImageDestinationCreateWithURL(url as CFURL, type.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, image, nil)
    guard CGImageDestinationFinalize(dest) else { fatalError("cannot write \(url.path)") }
}

func color(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> CGColor {
    CGColor(colorSpace: sRGB, components: [r, g, b, a])!
}

/// Draws `image` aspect-fit inside `rect`.
func draw(_ image: CGImage, in rect: CGRect, _ ctx: CGContext) {
    let s = min(rect.width / CGFloat(image.width), rect.height / CGFloat(image.height))
    let w = CGFloat(image.width) * s, h = CGFloat(image.height) * s
    ctx.draw(image, in: CGRect(x: rect.midX - w / 2, y: rect.midY - h / 2, width: w, height: h))
}

// MARK: - Icon

/// The box's brushed slate with the bronze emblem centred. `inset` is the margin around the rounded
/// square (macOS icons sit inside a 1024 grid with a ~10 % margin; Windows icons fill the canvas).
func icon(size: Int, inset: CGFloat, radius: CGFloat) -> CGImage {
    let ctx = canvas(size, size)
    let s = CGFloat(size)
    let rect = CGRect(x: s * inset, y: s * inset, width: s * (1 - 2 * inset), height: s * (1 - 2 * inset))
    let shape = CGPath(roundedRect: rect, cornerWidth: rect.width * radius, cornerHeight: rect.width * radius, transform: nil)

    if inset > 0 { // soft drop shadow, as macOS draws for app icons
        ctx.saveGState()
        ctx.setShadow(offset: CGSize(width: 0, height: -s * 0.012), blur: s * 0.03, color: color(0, 0, 0, 0.45))
        ctx.addPath(shape)
        ctx.setFillColor(color(0.2, 0.22, 0.24))
        ctx.fillPath()
        ctx.restoreGState()
    }

    ctx.saveGState()
    ctx.addPath(shape)
    ctx.clip()
    ctx.draw(slate, in: rect)
    // Small sizes need the emblem larger to stay legible.
    let fill: CGFloat = size <= 32 ? 1.0 : 0.9
    draw(emblem, in: rect.insetBy(dx: rect.width * (1 - fill) / 2, dy: rect.height * (1 - fill) / 2), ctx)
    // Light from the top, like the box render (over both, so the emblem's patch blends in).
    let light = CGGradient(colorsSpace: sRGB, colors: [color(1, 1, 1, 0.1), color(0, 0, 0, 0.18)] as CFArray, locations: [0, 1])!
    ctx.drawLinearGradient(light, start: CGPoint(x: rect.midX, y: rect.maxY), end: CGPoint(x: rect.midX, y: rect.minY), options: [])
    // Thin bronze rim, like the box edges.
    ctx.addPath(shape)
    ctx.setStrokeColor(color(0.85, 0.55, 0.3, 0.6))
    ctx.setLineWidth(max(1, s * 0.008))
    ctx.strokePath()
    ctx.restoreGState()
    return ctx.makeImage()!
}

// macOS .icns through an .iconset and iconutil.
let iconset = FileManager.default.temporaryDirectory.appendingPathComponent("SpatialEQ.iconset")
try? FileManager.default.removeItem(at: iconset)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
for base in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let name = scale == 1 ? "icon_\(base)x\(base).png" : "icon_\(base)x\(base)@2x.png"
        write(icon(size: base * scale, inset: 0.1, radius: 0.225), iconset.appendingPathComponent(name))
    }
}
write(icon(size: 1024, inset: 0.1, radius: 0.225), iconDir.appendingPathComponent("SpatialEQ-1024.png"))
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", iconset.path, "-o", iconDir.appendingPathComponent("SpatialEQ.icns").path]
try iconutil.run()
iconutil.waitUntilExit()
guard iconutil.terminationStatus == 0 else { fatalError("iconutil failed") }

// Windows .ico: PNG-compressed entries (Windows Vista and later).
func pngData(_ image: CGImage) -> Data {
    let data = NSMutableData()
    let dest = CGImageDestinationCreateWithData(data, UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, image, nil)
    CGImageDestinationFinalize(dest)
    return data as Data
}
let icoSizes = [16, 24, 32, 48, 64, 128, 256]
let pngs = icoSizes.map { pngData(icon(size: $0, inset: 0.02, radius: 0.18)) }
var ico = Data()
func le16(_ v: Int) { var x = UInt16(v).littleEndian; ico.append(Data(bytes: &x, count: 2)) }
func le32(_ v: Int) { var x = UInt32(v).littleEndian; ico.append(Data(bytes: &x, count: 4)) }
le16(0); le16(1); le16(icoSizes.count)
var offset = 6 + 16 * icoSizes.count
for (size, png) in zip(icoSizes, pngs) {
    ico.append(UInt8(size == 256 ? 0 : size)); ico.append(UInt8(size == 256 ? 0 : size))
    ico.append(0); ico.append(0)
    le16(1); le16(32); le32(png.count); le32(offset)
    offset += png.count
}
for png in pngs { ico.append(png) }
try ico.write(to: iconDir.appendingPathComponent("SpatialEQ.ico"))

// MARK: - Installer images

/// The box render has a white background, so it sits on white (or very light) artwork.
func boxOnWhite(width: Int, height: Int, boxRect: CGRect) -> CGImage {
    let ctx = canvas(width, height, opaque: true)
    ctx.setFillColor(color(1, 1, 1))
    ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
    draw(box, in: boxRect, ctx)
    return ctx.makeImage()!
}

// macOS Installer: drawn bottom-left under the steps sidebar (about 170 x 230 pt free), transparent
// elsewhere so the window keeps its own light or dark background.
func pkgBackground(scale: Int) -> CGImage {
    let ctx = canvas(620 * scale, 418 * scale)
    draw(box, in: CGRect(x: 14 * scale, y: 14 * scale, width: 160 * scale, height: 200 * scale), ctx)
    return ctx.makeImage()!
}
write(pkgBackground(scale: 1), installerDir.appendingPathComponent("pkg-background.png"))
write(pkgBackground(scale: 2), installerDir.appendingPathComponent("pkg-background@2x.png"))

// Disk image window (720 x 440 pt): the box on the left, the app and Applications icons on the right
// (positions are set in branding/dmg-settings.py), an arrow and a caption between them.
func dmgBackground(scale: Int) -> CGImage {
    let w = 720 * scale, h = 440 * scale, k = CGFloat(scale)
    let ctx = canvas(w, h, opaque: true)
    let gradient = CGGradient(colorsSpace: sRGB, colors: [color(1, 1, 1), color(0.93, 0.93, 0.94)] as CFArray, locations: [0, 1])!
    ctx.drawLinearGradient(gradient, start: CGPoint(x: 0, y: CGFloat(h)), end: .zero, options: [])
    draw(box, in: CGRect(x: 20 * k, y: 30 * k, width: 300 * k, height: 380 * k), ctx)
    // Arrow from the app (x 420) to Applications (x 610), at the icons' height (y 220 from the top).
    let y = CGFloat(h) - 220 * k
    ctx.setStrokeColor(color(0.85, 0.5, 0.25))
    ctx.setLineWidth(4 * k)
    ctx.setLineCap(.round)
    ctx.move(to: CGPoint(x: 480 * k, y: y)); ctx.addLine(to: CGPoint(x: 548 * k, y: y)); ctx.strokePath()
    ctx.move(to: CGPoint(x: 536 * k, y: y + 12 * k)); ctx.addLine(to: CGPoint(x: 550 * k, y: y))
    ctx.addLine(to: CGPoint(x: 536 * k, y: y - 12 * k)); ctx.strokePath()
    let text = NSAttributedString(string: "Drag SpatialEQ to Applications", attributes: [
        .font: NSFont.systemFont(ofSize: 15 * k, weight: .medium),
        .foregroundColor: NSColor(srgbRed: 0.25, green: 0.27, blue: 0.3, alpha: 1),
    ])
    let line = CTLineCreateWithAttributedString(text)
    let bounds = CTLineGetBoundsWithOptions(line, [])
    ctx.textPosition = CGPoint(x: 515 * k - bounds.width / 2, y: CGFloat(h) - 330 * k)
    CTLineDraw(line, ctx)
    return ctx.makeImage()!
}
write(dmgBackground(scale: 1), installerDir.appendingPathComponent("dmg-background.png"))
write(dmgBackground(scale: 2), installerDir.appendingPathComponent("dmg-background@2x.png"))

// Inno Setup: the tall image on the Welcome and Finish pages, and the small image in the page header,
// at 100 / 125 / 150 / 175 / 200 % DPI. Inno Setup wants 24-bit BMPs.
for (percent, factor) in [(100, 1.0), (125, 1.25), (150, 1.5), (175, 1.75), (200, 2.0)] {
    let w = Int((164 * factor).rounded()), h = Int((314 * factor).rounded())
    let full = CGRect(x: 0, y: 0, width: w, height: h)
    write(boxOnWhite(width: w, height: h, boxRect: full.insetBy(dx: CGFloat(w) * 0.04, dy: CGFloat(h) * 0.1)),
          installerDir.appendingPathComponent("wizard-\(percent).bmp"), .bmp)
    let s = Int((55 * factor).rounded())
    let small = canvas(s, s, opaque: true)
    small.setFillColor(color(1, 1, 1))
    small.fill(CGRect(x: 0, y: 0, width: s, height: s))
    small.draw(icon(size: s, inset: 0.02, radius: 0.18), in: CGRect(x: 0, y: 0, width: s, height: s))
    write(small.makeImage()!, installerDir.appendingPathComponent("wizard-small-\(percent).bmp"), .bmp)
}

print("Wrote icons to \(iconDir.path) and installer images to \(installerDir.path)")
