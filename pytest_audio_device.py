# Smoke test for the current ESP32-S3 audio application.
import pytest
from pytest_embedded import Dut
from pytest_embedded_idf.utils import idf_parametrize


@pytest.mark.generic
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_audio_device_services_start(dut: Dut) -> None:
    dut.expect(r'SD Card Background Monitor Task started', timeout=10)
    dut.expect(r'Audio player task started', timeout=10)
    dut.expect(r'Starting HTTP Server on port: 80', timeout=10)
