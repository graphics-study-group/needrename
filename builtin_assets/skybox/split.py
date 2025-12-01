import cv2
import py360convert
import numpy as np

HDR_ERP_FILE = "./HDR_029_Sky_Cloudy_Ref.hdr"

hdr_erp = cv2.imread(HDR_ERP_FILE, flags=cv2.IMREAD_ANYDEPTH)
cube_dict : dict[str, any] = py360convert.e2c(hdr_erp, 512, "bilinear", "dict")

tonemap = cv2.createTonemapMantiuk(2.2, 0.85, 1.2)

for k, v in cube_dict.items():
    cv2.imwrite(f"skybox_{k}.hdr", v)
    sdr = tonemap.process(v.copy())
    sdr = np.clip(sdr * 255, 0, 255)
    cv2.imwrite(f'skybox_{k}_tonemapped.png', sdr)
