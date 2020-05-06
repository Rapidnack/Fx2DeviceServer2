﻿using CyUSB;
using MonoLibUsb;
using MonoLibUsb.Profile;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Fx2DeviceServer
{
    public class Fx2Device : IDisposable
    {
        public enum EDeviceType
        {
            Unknown = 0,
            DAC = 1,
            ADC = 2,
			DAC_C = 3, // + control port
			ADC_C = 4, // + control port
		}

		public enum EVendorRequests
		{
			DeviceType = 0xc0,
			DeviceParam = 0xc1,
			SetSampleRate = 0xc2,
		}

		private static Dictionary<ushort, TcpListener> listenerDict = new Dictionary<ushort, TcpListener>();
		private TcpClient controlClient = null;
		protected const int TIMEOUT = 3000;

		protected EDeviceType DeviceType { get; private set; } = EDeviceType.Unknown;

		private ushort _controlPortNo = 0;
		protected ushort ControlPortNo
		{
			get
			{
				return _controlPortNo;
			}
			set
			{
				_controlPortNo = value;

				if (1024 <= ControlPortNo)
				{
					CancellationTokenSource cts = new CancellationTokenSource();
					var ct = cts.Token;
					Task.Run(() =>
					{
						TcpListener listener = CreateListener(ControlPortNo);
						try
						{
							listener.Start();
							var addresses = Dns.GetHostAddresses(Dns.GetHostName())
							.Where(p => p.ToString().Contains('.'));								
							Console.WriteLine($"{ControlPortNo}: {string.Join(" ", addresses)}");

							CancellationTokenSource tcpCts = null;
							while (!ct.IsCancellationRequested)
							{
								controlClient = listener.AcceptTcpClient();
								Console.WriteLine($"{ControlPortNo}: accepted");

								if (tcpCts != null)
								{
									tcpCts.Cancel();
								}

								tcpCts = new CancellationTokenSource();
								var tcpCt = tcpCts.Token;
								Task.Run(() =>
								{
									NumTcpClients++;
									try
									{
										using (NetworkStream ns = controlClient.GetStream())
										using (StreamReader sr = new StreamReader(ns, Encoding.UTF8))
										using (StreamWriter sw = new StreamWriter(ns, Encoding.UTF8))
										{
											while (!tcpCt.IsCancellationRequested)
											{
												string str = sr.ReadLine();
												if (str.Trim() == string.Empty)
													return; // keep alive
												Console.WriteLine($"{ControlPortNo}: [in] {str}");

												ProcessInput(sw, str);
											}
										}
									}
									catch (Exception)
									{
										// nothing to do
									}
									finally
									{
										NumTcpClients--;
										Console.WriteLine($"{ControlPortNo}: closed");
									}
								}, tcpCt);
							}
						}
						catch (SocketException ex) when (ex.ErrorCode == 10004)
						{
							// nothing to do
						}
						catch (OperationCanceledException)
						{
							// nothing to do
						}
						catch (Exception ex)
						{
							Console.WriteLine($"{ControlPortNo}: {ex.Message}");
						}
						finally
						{
							listener.Stop();
							//Console.WriteLine($"{ControlPortNo}: listener stopped");
						}
					}, ct);
				}
			}
		}

		protected string RemoteAddress
		{
			get
			{
				if (NumTcpClients == 0)
					return "127.0.0.1";

				try
				{
					return ((IPEndPoint)controlClient.Client.RemoteEndPoint).Address.ToString();
				}
				catch
				{
					return "127.0.0.1";
				}
			}
		}

		protected CancellationTokenSource Cts { get; set; } = new CancellationTokenSource();

		protected int NumTcpClients { get; private set; } = 0;

		protected MonoUsbDeviceHandle MonoDeviceHandle { get; private set; } = null;

        public CyUSBDevice USBDevice { get; private set; } = null;

        private MonoUsbProfile _usbProfile = null;
        public MonoUsbProfile USBProfile
        {
            get
            {
                return _usbProfile;
            }
            set
            {
                _usbProfile = value;
                MonoDeviceHandle = _usbProfile.OpenDeviceHandle();
            }
        }

        public Fx2Device(CyUSBDevice usbDevice, MonoUsbProfile usbProfile, EDeviceType deviceType = EDeviceType.Unknown)
        {
            if (usbDevice != null)
            {
                USBDevice = usbDevice;
            }
            if (usbProfile != null)
            {
                USBProfile = usbProfile;
            }
            DeviceType = deviceType;

            if (deviceType == EDeviceType.Unknown)
            {
                Console.WriteLine($"+ {this}");
            }
        }

        private bool disposed = false;
        public void Dispose()
        {
            Dispose(true);
        }
        protected virtual void Dispose(bool disposing)
        {
            if (!disposed)
            {
                if (disposing)
                {
                    if (Cts != null)
                    {
                        Cts.Cancel();
                    }
                    Console.WriteLine($"- {this}");
                }

                disposed = true;
            }
        }

        public override string ToString()
        {
            return $"{DeviceType}";
        }

        public static byte[] ReceiveVendorResponse(CyUSBDevice usbDevice, MonoUsbDeviceHandle monoDeviceHandle, byte reqCode, int length, ushort value = 0, ushort index = 0)
        {
            if (usbDevice != null)
            {
                CyControlEndPoint ctrlEpt = usbDevice.ControlEndPt;
                ctrlEpt.TimeOut = TIMEOUT;
                ctrlEpt.Direction = CyConst.DIR_FROM_DEVICE;
                ctrlEpt.ReqType = CyConst.REQ_VENDOR;
                ctrlEpt.Target = CyConst.TGT_DEVICE;
                ctrlEpt.ReqCode = reqCode;
                ctrlEpt.Value = value;
                ctrlEpt.Index = index;

                int bytes = length;
                byte[] buffer = new byte[bytes];
                ctrlEpt.XferData(ref buffer, ref bytes);
                if (bytes == buffer.Length)
                {
                    return buffer;
                }
            }
            else
            {
                short bytes = (short)length;
                byte[] data = new byte[bytes];
                byte requestType = CyConst.DIR_FROM_DEVICE + CyConst.REQ_VENDOR + CyConst.TGT_DEVICE;
                int ret = MonoUsbApi.ControlTransfer(monoDeviceHandle, requestType, reqCode, (short)value, (short)index, data, bytes, TIMEOUT);
                if (ret == data.Length)
                {
                    return data;
                }
            }

            return null;
        }

        public static bool SendVendorRequest(CyUSBDevice usbDevice, MonoUsbDeviceHandle monoDeviceHandle, byte reqCode, byte[] data, ushort value = 0, ushort index = 0)
        {
            if (data == null)
            {
                data = new byte[0];
            }

            if (usbDevice != null)
            {
                CyControlEndPoint ctrlEpt = usbDevice.ControlEndPt;
                ctrlEpt.TimeOut = TIMEOUT;
                ctrlEpt.Direction = CyConst.DIR_TO_DEVICE;
                ctrlEpt.ReqType = CyConst.REQ_VENDOR;
                ctrlEpt.Target = CyConst.TGT_DEVICE;
                ctrlEpt.ReqCode = reqCode;
                ctrlEpt.Value = value;
                ctrlEpt.Index = index;

                int bytes = data.Length;
                ctrlEpt.XferData(ref data, ref bytes);
                return bytes == data.Length;
            }
            else
            {
                short bytes = (short)data.Length;
                byte requestType = CyConst.DIR_TO_DEVICE + CyConst.REQ_VENDOR + CyConst.TGT_DEVICE;
                int ret = MonoUsbApi.ControlTransfer(monoDeviceHandle, requestType, reqCode, (short)value, (short)index, data, bytes, TIMEOUT);
                return ret == data.Length;
            }
        }

        protected byte[] ReceiveVendorResponse(byte reqCode, int length, ushort value = 0, ushort index = 0)
        {
            return ReceiveVendorResponse(USBDevice, MonoDeviceHandle, reqCode, length, value, index);
        }

        protected bool SendVendorRequest(byte reqCode, byte[] data, ushort value = 0, ushort index = 0)
        {
            return SendVendorRequest(USBDevice, MonoDeviceHandle, reqCode, data, value, index);
        }

		protected virtual void ProcessInput(StreamWriter sw, string s)
		{
			if (s.StartsWith("*Rate:"))
			{
				uint rate = uint.Parse(s.Split(':')[1]);

				byte[] response = ReceiveVendorResponse((byte)EVendorRequests.SetSampleRate, 4,
					(ushort)(rate & 0xffff), (ushort)((rate >> 16) & 0xffff));

				rate = (uint)(response[0] + (response[1] << 8) + (response[2] << 16) + (response[3] << 24));
				WriteLine(sw, rate.ToString());
			}
		}

		protected void WriteLine(StreamWriter sw, string s)
		{
			sw.WriteLine(s);
			sw.Flush();
			Console.WriteLine($"{ControlPortNo}: [out] {s}");
		}

		private static TcpListener CreateListener(ushort port)
		{
			if (listenerDict.ContainsKey(port))
			{
				TcpListener oldListener = listenerDict[port];
				oldListener.Stop();
				listenerDict.Remove(port);
			}
			TcpListener newListener = new TcpListener(IPAddress.Any, port);
			listenerDict.Add(port, newListener);

			return newListener;
		}
	}
}
