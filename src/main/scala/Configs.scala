package testchipip

import chisel3._
import freechips.rocketchip.system.BaseConfig
import freechips.rocketchip.config.{Parameters, Config}
import freechips.rocketchip.diplomacy.BufferParams
import freechips.rocketchip.subsystem.{BuildSystemBus, SystemBusKey, CacheBlockBytes}
import freechips.rocketchip.tile.XLen
import freechips.rocketchip.unittest.UnitTests

class WithRingSystemBus(buffer: BufferParams = BufferParams.default)
    extends Config((site, here, up) => {
  case BuildSystemBus => (p: Parameters) =>
    new RingSystemBus(p(SystemBusKey), buffer)(p)
})

class WithMeshSystemBus(buffer: BufferParams = BufferParams.default)
    extends Config((site, here, up) => {
  case BuildSystemBus => (p: Parameters) =>
    new MeshSystemBus(p(SystemBusKey), buffer)(p)
})

class WithTestChipUnitTests extends Config((site, here, up) => {
  case UnitTests => (testParams: Parameters) =>
    TestChipUnitTests(testParams)
})

class TestChipUnitTestConfig extends Config(
  new WithTestChipUnitTests ++ new BaseConfig)

class WithBlockDevice extends Config((site, here, up) => {
  case BlockDeviceKey => BlockDeviceConfig()
})

class WithNBlockDeviceTrackers(n: Int) extends Config((site, here, up) => {
  case BlockDeviceKey => up(BlockDeviceKey, site).copy(nTrackers = n)
})
