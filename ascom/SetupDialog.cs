using System;
using System.Windows.Forms;

namespace AstroMount
{
    /// <summary>
    /// ASCOM setup dialog for the AstroMount Telescope driver.
    /// Lets the user configure the mount controller gRPC endpoint
    /// (host, port, optional TLS) and test the connection before applying.
    /// </summary>
    public class SetupDialog : Form
    {
        private readonly TextBox _txtHost;
        private readonly NumericUpDown _numPort;
        private readonly CheckBox _chkSsl;
        private readonly Button _btnTest;
        private readonly Button _btnOk;
        private readonly Button _btnCancel;
        private readonly Label _lblStatus;

        /// <summary>Configured gRPC host.</summary>
        public string Host => _txtHost.Text.Trim();

        /// <summary>Configured gRPC port.</summary>
        public int Port => (int)_numPort.Value;

        /// <summary>Whether TLS should be used.</summary>
        public bool UseSsl => _chkSsl.Checked;

        public SetupDialog(string host, int port, bool useSsl = false)
        {
            Text = "AstroMount Telescope — Setup";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new System.Drawing.Size(380, 210);

            var lblHost = new Label { Text = "gRPC Host:", Location = new System.Drawing.Point(12, 20), AutoSize = true };
            _txtHost = new TextBox { Text = host, Location = new System.Drawing.Point(120, 17), Width = 200 };

            var lblPort = new Label { Text = "Port:", Location = new System.Drawing.Point(12, 55), AutoSize = true };
            _numPort = new NumericUpDown
            {
                Location = new System.Drawing.Point(120, 52),
                Width = 100,
                Minimum = 1,
                Maximum = 65535,
                Value = port,
            };

            _chkSsl = new CheckBox
            {
                Text = "Use TLS (SSL)",
                Location = new System.Drawing.Point(120, 85),
                AutoSize = true,
                Checked = useSsl,
            };

            _lblStatus = new Label
            {
                Text = "",
                Location = new System.Drawing.Point(12, 115),
                Size = new System.Drawing.Size(356, 30),
                ForeColor = System.Drawing.Color.DimGray,
            };

            _btnTest = new Button { Text = "Test Connection", Location = new System.Drawing.Point(12, 155), Width = 110 };
            _btnOk = new Button { Text = "OK", Location = new System.Drawing.Point(220, 155), Width = 70, DialogResult = DialogResult.OK };
            _btnCancel = new Button { Text = "Cancel", Location = new System.Drawing.Point(296, 155), Width = 70, DialogResult = DialogResult.Cancel };

            _btnTest.Click += (s, e) => TestConnection();

            Controls.AddRange(new Control[] { lblHost, _txtHost, lblPort, _numPort, _chkSsl, _lblStatus, _btnTest, _btnOk, _btnCancel });
            AcceptButton = _btnOk;
            CancelButton = _btnCancel;
        }

        /// <summary>
        /// Attempt to connect to the configured endpoint and report the result.
        /// </summary>
        private void TestConnection()
        {
            _lblStatus.Text = "Testing…";
            _lblStatus.ForeColor = System.Drawing.Color.DimGray;
            Cursor = Cursors.WaitCursor;
            try
            {
                using (var client = new GrpcClient(Host, Port, UseSsl))
                {
                    client.Connect();
                    var health = client.CheckHealth();
                    _lblStatus.Text = "OK — controller is SERVING.";
                    _lblStatus.ForeColor = System.Drawing.Color.Green;
                }
            }
            catch (Exception ex)
            {
                _lblStatus.Text = "FAILED: " + ex.Message;
                _lblStatus.ForeColor = System.Drawing.Color.Red;
            }
            finally
            {
                Cursor = Cursors.Default;
            }
        }
    }
}
