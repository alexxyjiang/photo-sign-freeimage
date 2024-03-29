 /** signchooser.cpp - Choose the right sign for photo
 *  @author:  Alex.J. (alexxyjiang@gmail.com)
 *  @date:    2016-10-07
 */

#include <string>
#include <sstream>
#include <cmath>
#include <dirent.h>
#include "FreeImagePlus.h"
#include "hsvcolor.h"
#include "signchooser.h"

DEFINE_bool(auto_sign_color, false, "auto decide which color to use for sign");

namespace PhotoMgr {
  // ColorMgr - public
  ColorMgr::ColorMgr(const fipImage *p_image) {
    set_fip_image(p_image);
  }

  void ColorMgr::set_fip_image(const fipImage *p_image) {
    this->_p_image  = p_image;
  }

  RGBQUAD ColorMgr::mean_color() const {
    RGBQUAD rgb;
    unsigned long long red_sum        = 0;
    unsigned long long green_sum      = 0;
    unsigned long long blue_sum       = 0;
    unsigned long long alpha_sum      = 0;
    unsigned long long pixel_count    = 0;
    FREE_IMAGE_COLOR_TYPE color_type  = _p_image->getColorType();
    for (unsigned i = 0; i < _p_image->getHeight(); ++i) {
      for (unsigned j = 0; j < _p_image->getWidth(); ++j) {
        if (_p_image->getPixelColor(j, i, &rgb)) {
          if ((color_type == FIC_RGBALPHA && rgb.rgbReserved > 0) || color_type == FIC_RGB) {
            red_sum   += rgb.rgbRed;
            green_sum += rgb.rgbGreen;
            blue_sum  += rgb.rgbBlue;
            alpha_sum += rgb.rgbReserved;
            ++pixel_count;
          }
        }
      }
    }
    rgb.rgbRed      = red_sum   / pixel_count;
    rgb.rgbGreen    = green_sum / pixel_count;
    rgb.rgbBlue     = blue_sum  / pixel_count;
    rgb.rgbReserved = alpha_sum / pixel_count;

    return rgb;
  }

  double ColorMgr::color_distance(const ColorMgr &other) const {
    RGBQUAD mean_self, mean_other;
    mean_self   = mean_color();
    mean_other  = other.mean_color();

    // alpha ignored
    double dis_red, dis_green, dis_blue, distance;
    dis_red   = fabs(double(mean_self.rgbRed   - mean_other.rgbRed));
    dis_green = fabs(double(mean_self.rgbGreen - mean_other.rgbGreen));
    dis_blue  = fabs(double(mean_self.rgbBlue  - mean_other.rgbBlue));

    distance  = sqrt(dis_red * dis_red + dis_green * dis_green + dis_blue * dis_blue);
    return distance;
  }

  // SignDrawer - public
  SignDrawer::SignDrawer() {
    _vec_sign_library.reserve(4);
  }

  void SignDrawer::clear_library() {
    for (std::vector<fipImage *>::const_iterator iter = _vec_sign_library.begin(); iter != _vec_sign_library.end(); ++iter) {
      (*iter)->clear();
    }
    _vec_sign_library.clear();
  }

  int SignDrawer::load_library(const std::string &base_path) {
    DIR *dp;
    struct dirent *ep;
    dp  = opendir(base_path.c_str());
    if (dp) {
      while (NULL != (ep = readdir(dp))) {
        fipImage    *p_fimg = new fipImage();
        std::ostringstream  ostr_fullname;
        ostr_fullname << base_path << "/" << ep->d_name;
        if (p_fimg->load(ostr_fullname.str().c_str())) {
          _vec_sign_library.push_back(p_fimg);
        }
      }
      closedir(dp);
    }
    return 0;
  }

  int SignDrawer::sign_photo(fipImage *p_dest, const fipImage *p_src, const sign_conf_t sign_conf) {
    fipImage  *p_sign_chosen  = NULL;
    double    best_distance   = 0.0;
    unsigned  best_x0, best_x1, best_y0, best_y1, best_scale;
    best_x0 = best_x1 = best_y0 = best_y1 = 0;
    best_scale  = 1;
    for (std::vector<fipImage *>::const_iterator iter = _vec_sign_library.begin(); iter != _vec_sign_library.end(); ++iter) {
      image_size_t  photo_size, sign_size;
      photo_size.width  = p_src->getWidth();
      photo_size.height = p_src->getHeight();

      sign_size.width   = (*iter)->getWidth();
      sign_size.height  = (*iter)->getHeight();

      SignPosiMgr sign_pm(photo_size, sign_size, sign_conf);
      if (0 == sign_pm.calc_sign_posi()) {
        unsigned  sign_x0 = sign_pm.sign_x0();
        unsigned  sign_x1 = sign_pm.sign_x1();
        unsigned  sign_y0 = sign_pm.sign_y0();
        unsigned  sign_y1 = sign_pm.sign_y1();

        fipImage  sub_image;
        if (p_src->copySubImage(sub_image, sign_x0, sign_y0, sign_x1, sign_y1)) {
          ColorMgr  cmgr_sub(&sub_image);
          ColorMgr  cmgr_sign(*iter);
          double    dist  = cmgr_sub.color_distance(cmgr_sign);
          if (dist > best_distance + 1e-6) {
            p_sign_chosen = *iter;
            best_distance = dist;
            best_x0       = sign_x0;
            best_y0       = sign_y0;
            best_x1       = sign_x1;
            best_y1       = sign_y1;
            best_scale    = sign_pm.sign_conf().scale_rate;
          }
        }
      }
    }

    if (p_sign_chosen != NULL) {
      (*p_dest) = (*p_src);
      fipImage  fip_sign;
      fip_sign  = (*p_sign_chosen);
      if (FLAGS_auto_sign_scale) {
        fip_sign.rescale(p_sign_chosen->getWidth() * best_scale, p_sign_chosen->getHeight() * best_scale, FILTER_LANCZOS3);
      }
      RGBQUAD reverse_color;
      if (FLAGS_auto_sign_color) {
        fipImage  sub_image_sign;
        if (p_dest->copySubImage(sub_image_sign, best_x0, best_y0, best_x1, best_y1)) {
          ColorMgr  cmgr_sub_sign(&sub_image_sign);
          RGBQUAD   mean_color    = cmgr_sub_sign.mean_color();
          HSVACOLOR mean_hsva     = convert_rgba2hsva(mean_color);
          HSVACOLOR reverse_hsva  = reversed_hsva_color(mean_hsva);
          if (reverse_hsva.s < 0.3820) {
            reverse_hsva.s = 0.6180;
          } else {
            reverse_hsva.s = 1.0;
          }
          if (reverse_hsva.v < 0.3820) {
            reverse_hsva.v = 0.6180;
          } else {
            reverse_hsva.v = 1.0;
          }
          reverse_color = convert_hsva2rgba(reverse_hsva);
        }
      }

      for (unsigned i = 0; i < fip_sign.getHeight(); ++i) {
        for (unsigned j = 0; j < fip_sign.getWidth(); ++j) {
          RGBQUAD rgb_dest, rgb_sign;
          if (p_dest->getPixelColor(best_x0 + j, best_y0 + i, &rgb_dest) && fip_sign.getPixelColor(j, i, &rgb_sign) && rgb_sign.rgbReserved > 0) {
            if (FLAGS_auto_sign_color) {
              rgb_dest.rgbRed       = ((unsigned)rgb_dest.rgbRed    * (255 - rgb_sign.rgbReserved) + (unsigned)reverse_color.rgbRed    * rgb_sign.rgbReserved) / 255;
              rgb_dest.rgbGreen     = ((unsigned)rgb_dest.rgbGreen  * (255 - rgb_sign.rgbReserved) + (unsigned)reverse_color.rgbGreen  * rgb_sign.rgbReserved) / 255;
              rgb_dest.rgbBlue      = ((unsigned)rgb_dest.rgbBlue   * (255 - rgb_sign.rgbReserved) + (unsigned)reverse_color.rgbBlue   * rgb_sign.rgbReserved) / 255;
              rgb_dest.rgbReserved  = 0;
            } else {
              rgb_dest.rgbRed       = ((unsigned)rgb_dest.rgbRed    * (255 - rgb_sign.rgbReserved) + (unsigned)rgb_sign.rgbRed    * rgb_sign.rgbReserved) / 255;
              rgb_dest.rgbGreen     = ((unsigned)rgb_dest.rgbGreen  * (255 - rgb_sign.rgbReserved) + (unsigned)rgb_sign.rgbGreen  * rgb_sign.rgbReserved) / 255;
              rgb_dest.rgbBlue      = ((unsigned)rgb_dest.rgbBlue   * (255 - rgb_sign.rgbReserved) + (unsigned)rgb_sign.rgbBlue   * rgb_sign.rgbReserved) / 255;
              rgb_dest.rgbReserved  = 0;
            }

            p_dest->setPixelColor(best_x0 + j, best_y0 + i, &rgb_dest);
          }
        }
      }
      return 0;
    } else {
      return -1;
    }
  }

  // SignPhotoMgr - public
  SignPhotoMgr::SignPhotoMgr(const std::string &photo_path, const std::string &sign_path) {
    set_photo_path(photo_path);
    set_sign_path(sign_path);
  }

  void SignPhotoMgr::set_photo_path(const std::string &photo_path) {
    this->_photo_path = photo_path;
  }

  void SignPhotoMgr::set_sign_path(const std::string &sign_path) {
    this->_sign_path  = sign_path;
  }

  int SignPhotoMgr::sign_all_photos(const sign_conf_t sign_conf) {
    SignDrawer  sdr;
    fipImage    fip_src, fip_dest;
    if (0 == sdr.load_library(_sign_path)) {
      DIR *dp;
      struct dirent *ep;
      dp  = opendir(_photo_path.c_str());
      if (dp) {
        while (NULL != (ep = readdir(dp))) {
          std::ostringstream  ostr_fullname;
          ostr_fullname << _photo_path << "/" << ep->d_name;
          if (fip_src.load(ostr_fullname.str().c_str()) && 0 == sdr.sign_photo(&fip_dest, &fip_src, sign_conf)) {
            std::ostringstream  ostr;
            ostr  << _photo_path << "/SIGN_" << ep->d_name;
            fip_dest.save(ostr.str().c_str(), JPEG_QUALITYSUPERB);
          }
        }
      } else {
        return -1;
      }
    } else {
      return -1;
    }
    return 0;
  }
} // namespace PhotoMgr
