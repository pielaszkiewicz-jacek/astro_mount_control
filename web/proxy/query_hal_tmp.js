const path = require('path');
const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');
const P = path.join('/home/jacek/development/astro_mount_control', 'proto/mount_controller.proto');
const pkg = protoLoader.loadSync(P, {keepCase:true, longs:String, enums:String, defaults:true, oneofs:true});
const proto = grpc.loadPackageDefinition(pkg).astro_mount;
const client = new proto.MountControllerService('127.0.0.1:50051', grpc.credentials.createInsecure());
const deadline = new Date(); deadline.setSeconds(deadline.getSeconds()+5);
client.GetHALConfig({}, {deadline}, (err, resp) => {
  if (err) { console.error('ERR', err.message); process.exit(1); }
  const c = resp.config || resp;
  console.log('type=', c.type, 'name=', c.name);
  (c.axes || []).forEach(a => {
    const m = a.motor_config || {};
    console.log('axis', a.id, 'motor.encoder_counts_per_degree=', m.encoder_counts_per_degree, 'gear=', m.gear_ratio);
    const e = a.encoder_config || {};
    console.log('       enc.counts_per_degree=', e.counts_per_degree, 'resolution_counts=', e.resolution_counts);
  });
  process.exit(0);
});
