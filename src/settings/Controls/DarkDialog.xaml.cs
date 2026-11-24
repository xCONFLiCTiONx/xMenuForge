using System.Windows;

namespace xMenuForge.Settings.Controls;

public partial class DarkDialog : Window
{
    public string InputValue => InputBox.Text;

    public DarkDialog(string title, string message, bool showCancel = false, bool showInput = false, string defaultInput = "")
    {
        InitializeComponent();
        TitleText.Text = title;
        MessageText.Text = message;
        if (showCancel) CancelBtn.Visibility = Visibility.Visible;
        if (showInput)
        {
            InputBox.Visibility = Visibility.Visible;
            InputBox.Text = defaultInput;
            Loaded += (_, _) => { InputBox.Focus(); InputBox.SelectAll(); };
        }
    }

    public static void ShowInfo(Window owner, string title, string message)
    {
        var dlg = new DarkDialog(title, message) { Owner = owner };
        dlg.ShowDialog();
    }

    public static bool Confirm(Window owner, string title, string message)
    {
        var dlg = new DarkDialog(title, message, showCancel: true) { Owner = owner };
        return dlg.ShowDialog() == true;
    }

    public static string? Prompt(Window owner, string title, string message, string defaultValue = "")
    {
        var dlg = new DarkDialog(title, message, showCancel: true, showInput: true, defaultInput: defaultValue) { Owner = owner };
        return dlg.ShowDialog() == true ? dlg.InputValue : null;
    }

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = true;
        Close();
    }

    private void Cancel_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }
}
