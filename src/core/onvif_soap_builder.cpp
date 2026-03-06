#include "core/onvif_soap_builder.h"
#include <sstream>

std::string ONVIFSoapBuilder::buildGetDeviceInformation() {
  std::ostringstream oss;
  oss << "    <tds:GetDeviceInformation/>\n";
  return buildSoapEnvelope(oss.str(),
                            "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation",
                            "");
}

std::string ONVIFSoapBuilder::buildGetCapabilities() {
  std::ostringstream oss;
  oss << "    <tds:GetCapabilities>\n"
      << "      <tds:Category>All</tds:Category>\n"
      << "    </tds:GetCapabilities>\n";
  return buildSoapEnvelope(oss.str(),
                            "http://www.onvif.org/ver10/device/wsdl/GetCapabilities",
                            "");
}

std::string ONVIFSoapBuilder::buildGetProfiles() {
  std::ostringstream oss;
  oss << "    <trt:GetProfiles/>\n";
  return buildSoapEnvelope(oss.str(),
                           "http://www.onvif.org/ver10/media/wsdl/GetProfiles",
                           "");
}

std::string ONVIFSoapBuilder::buildGetStreamUri(
    const std::string &profileToken, const std::string &streamType,
    const std::string &protocol) {
  std::ostringstream oss;
  oss << "    <trt:GetStreamUri>\n"
      << "      <trt:StreamSetup>\n"
      << "        <tt:Stream>" << streamType << "</tt:Stream>\n"
      << "        <tt:Transport>\n"
      << "          <tt:Protocol>" << protocol << "</tt:Protocol>\n"
      << "        </tt:Transport>\n"
      << "      </trt:StreamSetup>\n"
      << "      <trt:ProfileToken>" << profileToken << "</trt:ProfileToken>\n"
      << "    </trt:GetStreamUri>\n";
  return buildSoapEnvelope(oss.str(),
                             "http://www.onvif.org/ver10/media/wsdl/GetStreamUri",
                             "");
}

std::string ONVIFSoapBuilder::buildGetVideoEncoderConfiguration(
    const std::string &configurationToken) {
  std::ostringstream oss;
  oss << "    <trt:GetVideoEncoderConfiguration>\n"
      << "      <trt:ConfigurationToken>" << configurationToken
      << "</trt:ConfigurationToken>\n"
      << "    </trt:GetVideoEncoderConfiguration>\n";
  return buildSoapEnvelope(oss.str(),
                            "http://www.onvif.org/ver10/media/wsdl/GetVideoEncoderConfiguration",
                            "");
}

std::string ONVIFSoapBuilder::buildGetVideoEncoderConfigurations() {
  std::ostringstream oss;
  oss << "    <trt:GetVideoEncoderConfigurations/>\n";
  return buildSoapEnvelope(oss.str(),
                           "http://www.onvif.org/ver10/media/wsdl/GetVideoEncoderConfigurations",
                           "");
}

std::string ONVIFSoapBuilder::buildGetSnapshotUri(const std::string &profileToken) {
  std::ostringstream oss;
  oss << "    <trt:GetSnapshotUri>\n"
      << "      <trt:ProfileToken>" << profileToken << "</trt:ProfileToken>\n"
      << "    </trt:GetSnapshotUri>\n";
  return buildSoapEnvelope(oss.str(),
                            "http://www.onvif.org/ver10/media/wsdl/GetSnapshotUri",
                            "");
}

std::string ONVIFSoapBuilder::buildPTZGetStatus(const std::string &profileToken) {
  std::ostringstream oss;
  oss << "    <tptz:GetStatus>\n"
      << "      <tptz:ProfileToken>" << profileToken << "</tptz:ProfileToken>\n"
      << "    </tptz:GetStatus>\n";
  return buildSoapEnvelope(oss.str(),
                            "http://www.onvif.org/ver20/ptz/wsdl/GetStatus",
                            "");
}

std::string ONVIFSoapBuilder::buildPTZAbsoluteMove(
    const std::string &profileToken, double pan, double tilt, double zoom) {
  std::ostringstream oss;
  oss << "    <tptz:AbsoluteMove>\n"
      << "      <tptz:ProfileToken>" << profileToken << "</tptz:ProfileToken>\n"
      << "      <tptz:Position>\n"
      << "        <tt:PanTilt x=\"" << pan << "\" y=\"" << tilt << "\"/>\n"
      << "        <tt:Zoom x=\"" << zoom << "\"/>\n"
      << "      </tptz:Position>\n"
      << "    </tptz:AbsoluteMove>\n";
  return buildSoapEnvelope(oss.str(),
                            "http://www.onvif.org/ver20/ptz/wsdl/AbsoluteMove",
                            "");
}

std::string ONVIFSoapBuilder::buildGetServiceCapabilities() {
  std::ostringstream oss;
  oss << "    <trt:GetServiceCapabilities/>\n";
  return buildSoapEnvelope(oss.str(),
                            "http://www.onvif.org/ver10/media/wsdl/GetServiceCapabilities",
                            "");
}

std::string ONVIFSoapBuilder::buildSoapHeader(const std::string &action,
                                               const std::string &to) {
  std::ostringstream oss;
  oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"\n"
      << "            xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\"\n"
      << "            xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\"\n"
      << "            xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\"\n"
      << "            xmlns:tt=\"http://www.onvif.org/ver10/schema\">\n"
      << "  <s:Header>\n";
  
  if (!action.empty()) {
    oss << "    <wsa:Action xmlns:wsa=\"http://www.w3.org/2005/08/addressing\">"
        << action << "</wsa:Action>\n";
  }
  
  if (!to.empty()) {
    oss << "    <wsa:To xmlns:wsa=\"http://www.w3.org/2005/08/addressing\">"
        << to << "</wsa:To>\n";
  }
  
  oss << "  </s:Header>\n"
      << "  <s:Body>\n";
  
  return oss.str();
}

std::string ONVIFSoapBuilder::buildSoapFooter() {
  return "  </s:Body>\n</s:Envelope>";
}

std::string ONVIFSoapBuilder::buildSoapEnvelope(const std::string &body,
                                                 const std::string &action,
                                                 const std::string &to) {
  return buildSoapHeader(action, to) + body + buildSoapFooter();
}

