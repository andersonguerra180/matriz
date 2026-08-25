import os
import subprocess
import shutil
from PIL import Image

def generate_icons():
    # Paths
    base_dir = "/Volumes/BUNKER 4TB/Apps/BKR Matriz"
    assets_dir = os.path.join(base_dir, "Assets")
    icon_jpeg_path = os.path.join(assets_dir, "icon.jpeg")
    iconset_dir = os.path.join(assets_dir, "Icon.iconset")
    
    if not os.path.exists(icon_jpeg_path):
        print(f"Error: {icon_jpeg_path} not found.")
        return

    # Clean and recreate iconset directory to avoid any leftover files
    if os.path.exists(iconset_dir):
        shutil.rmtree(iconset_dir)
    os.makedirs(iconset_dir, exist_ok=True)
    
    # Open source image and strictly convert to RGBA
    # iconutil requires alpha channel (32-bit RGBA PNG)
    img = Image.open(icon_jpeg_path).convert("RGBA")
    
    # Ensure it's square
    w, h = img.size
    if w != h:
        print(f"Warning: Source image is not square ({w}x{h}). Centering and cropping...")
        min_dim = min(w, h)
        left = (w - min_dim) / 2
        top = (h - min_dim) / 2
        right = (w + min_dim) / 2
        bottom = (h + min_dim) / 2
        img = img.crop((left, top, right, bottom))
    
    # Define sizes for macOS iconset
    mac_sizes = [
        ("icon_16x16.png", (16, 16)),
        ("icon_16x16@2x.png", (32, 32)),
        ("icon_32x32.png", (32, 32)),
        ("icon_32x32@2x.png", (64, 64)),
        ("icon_128x128.png", (128, 128)),
        ("icon_128x128@2x.png", (256, 256)),
        ("icon_256x256.png", (256, 256)),
        ("icon_256x256@2x.png", (512, 512)),
        ("icon_512x512.png", (512, 512)),
        ("icon_512x512@2x.png", (1024, 1024))
    ]
    
    # Generate PNG files for macOS iconset
    print("Generating macOS iconset PNG files...")
    for filename, size in mac_sizes:
        dest_path = os.path.join(iconset_dir, filename)
        resized_img = img.resize(size, Image.Resampling.LANCZOS)
        resized_img.save(dest_path, "PNG")
        print(f"Created: {dest_path} ({size[0]}x{size[1]})")
        
    # Generate .icns file using macOS native iconutil tool
    icns_path = os.path.join(assets_dir, "icon.icns")
    print(f"Generating {icns_path} using iconutil...")
    try:
        subprocess.run(["iconutil", "-c", "icns", iconset_dir, "-o", icns_path], check=True)
        print("Successfully generated icon.icns")
    except Exception as e:
        print(f"Error running iconutil: {e}")
        
    # Generate favicon.ico (multi-resolution ICO file: 16x16, 32x32, 48x48)
    favicon_ico_path = os.path.join(assets_dir, "favicon.ico")
    print(f"Generating favicon.ico at {favicon_ico_path}...")
    ico_sizes = [(16, 16), (32, 32), (48, 48)]
    ico_imgs = [img.resize(size, Image.Resampling.LANCZOS) for size in ico_sizes]
    # Save first image as .ico with others embedded as sizes
    ico_imgs[0].save(favicon_ico_path, format="ICO", sizes=ico_sizes, append_images=ico_imgs[1:])
    print("Successfully generated favicon.ico")
    
    # Generate favicon.png (standard 32x32 and 180x180 Apple Touch Icon, etc.)
    favicon_png_path = os.path.join(assets_dir, "favicon.png")
    img.resize((32, 32), Image.Resampling.LANCZOS).save(favicon_png_path, "PNG")
    print(f"Successfully generated favicon.png (32x32) at {favicon_png_path}")
    
    apple_touch_icon_path = os.path.join(assets_dir, "apple-touch-icon.png")
    img.resize((180, 180), Image.Resampling.LANCZOS).save(apple_touch_icon_path, "PNG")
    print(f"Successfully generated apple-touch-icon.png (180x180) at {apple_touch_icon_path}")

if __name__ == "__main__":
    generate_icons()
