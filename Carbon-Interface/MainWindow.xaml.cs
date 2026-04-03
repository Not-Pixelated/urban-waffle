using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;

namespace Carbon_Interface
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
        }

        private void InjectBtn_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                var psi = new System.Diagnostics.ProcessStartInfo()
                {
                    FileName = "Carbon-Injector.exe",
                    WorkingDirectory = AppDomain.CurrentDomain.BaseDirectory,
                    UseShellExecute = true,
                };
                System.Diagnostics.Process.Start(psi);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Failed to launch injector. Ensure you built Carbon-Injector first.\n\n" + ex.Message, "Injection Failed");
            }
        }

        private async void ExecuteBtn_Click(object sender, RoutedEventArgs e)
        {
            string script = ScriptBox.Text;
            if (string.IsNullOrWhiteSpace(script)) return;

            try
            {
                using var pipe = new System.IO.Pipes.NamedPipeClientStream(".", "CarbonExecution", System.IO.Pipes.PipeDirection.Out);
                await pipe.ConnectAsync(1000);
                
                using var writer = new System.IO.StreamWriter(pipe);
                writer.AutoFlush = true;
                await writer.WriteAsync(script);
            }
            catch (TimeoutException)
            {
                MessageBox.Show("Could not connect to Carbon. Did you inject successfully?", "Execution Failed");
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error");
            }
        }
    }
}