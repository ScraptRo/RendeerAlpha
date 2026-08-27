"""A Rendeer application, in Python.

Run it with the runtime's directory on hand:

    python hello.py

Nothing here knows about Vulkan, windows, or the other applications sharing the same
graphics stack. This process builds a description of a frame and commits it; the runtime
owns the device and does the drawing.
"""
import math
import rda

app = rda.App("Hello from Python", 800, 600)
clicks = 0
checker = None


@app.on_start
def start():
    # A picture built here and uploaded once. This process has no device of its own, so it
    # sends the bytes and keeps the handle the runtime files them under.
    global checker
    size = 64
    pixels = bytearray(size * size * 4)
    for y in range(size):
        for x in range(size):
            i = (y * size + x) * 4
            light = ((x >> 3) + (y >> 3)) % 2 == 0
            pixels[i] = 230 if light else 40
            pixels[i + 1] = (x * 4) & 0xFF
            pixels[i + 2] = (y * 4) & 0xFF
            pixels[i + 3] = 255
    checker = app.create_texture(size, size, bytes(pixels))


@app.on_close
def close():
    # Disconnecting would release it anyway, but an application that uploads more than it
    # keeps should say so as it goes rather than leaving it to its own exit.
    if checker:
        checker.destroy()


@app.draw
def draw(ui):
    global clicks
    ui.rect(0, 0, ui.width, ui.height, 0x1C202Cff)

    # A title bar, with the text centred by measuring it rather than guessing.
    ui.rect(0, 0, ui.width, 56, 0x3A5CA8ff)
    title = "Hello from Python"
    ui.text(title, (ui.width - ui.measure(title)) / 2, 18, 0xF0F2F8ff)

    # A button that reacts to the pointer the runtime forwards.
    bx, by, bw, bh = 40, 100, 220, 44
    over = ui.hit(bx, by, bw, bh)
    if over:
        colour = 0xF0A050ff if ui.pointer_down else 0x4A6FBEff
    else:
        colour = 0x2E3444ff
    ui.rect(bx, by, bw, bh, colour)
    label = "Clicked %d times" % clicks
    ui.text(label, bx + (bw - ui.measure(label)) / 2, by + 14, 0xE8EAF0ff)
    if over and ui.pressed:
        clicks += 1

    # Something animated, so an unchanged frame is visibly not the normal case.
    for i in range(16):
        h = 24 + 40 * (0.5 + 0.5 * math.sin(ui.time * 2 + i * 0.4))
        ui.rect(40 + i * 26, ui.height - 40 - h, 18, h, 0x5A8CF0ff)

    # Drawing with one the runtime has not confirmed is refused, so ask first.
    if checker and checker.ready:
        ui.image(ui.width - 128, 96, checker.width, checker.height, checker)

    ui.text("pointer %d, %d" % (ui.pointer_x, ui.pointer_y), 40, 170, 0x8A93A8ff)


app.run()
