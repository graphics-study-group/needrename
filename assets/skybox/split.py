import os
os.environ["OPENCV_IO_ENABLE_OPENEXR"] = "1"

import cv2
import py360convert
import numpy as np


HDR_ERP_FILE = "./185_hdrmaps_com_free_1K.exr"

hdr_erp = cv2.imread(HDR_ERP_FILE, flags=cv2.IMREAD_ANYCOLOR | cv2.IMREAD_ANYDEPTH)
hdr_erp = hdr_erp.clip(0, 1)
cv2.imshow("", hdr_erp)
print(np.max(hdr_erp))
cv2.waitKey(0)

tonemapDurand = cv2.createTonemap(2.2)
erp = hdr_erp
cv2.imshow("", erp)
cv2.waitKey(0)

cube_dict : dict[str, any] = py360convert.e2c(erp, 512, "bilinear", "dict")

for k, v in cube_dict.items():
    sdr = np.clip(v * 255, 0, 255)
    cv2.imwrite(f'skybox_{k}_tonemapped.png', sdr)
